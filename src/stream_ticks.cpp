
// Program to stream tickdata from file over websocket ==> 
// Author :: Tapan Hazarika
// Jan 17, 23:35 IST, 2026

#include <deque>
#include <mutex>
#include <zstd.h>
#include <atomic>
#include <bitset>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>
#include <simdjson.h>
#include <unordered_map>
#include <unordered_set>
#include <boost/asio.hpp>
#include <condition_variable>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

// ========================================================================

#ifdef _MSC_VER
    #define ALWAYS_INLINE inline
    #define HOT_FUNCTION
    #define FLATTEN
#else
    #define ALWAYS_INLINE __attribute__((always_inline)) inline
    #define HOT_FUNCTION __attribute__((hot, flatten))
    #define FLATTEN __attribute__((flatten))
#endif

// ========================================================================

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

using tcp = net::ip::tcp;

// ========================================================================

#pragma pack(push, 1)
struct TickFields {
    uint32_t timestamp_delta;
    uint16_t symbol_id;

    int32_t lp, ti, pc, o, h, l, c, ap, uc, lc, h52, l52;
    int32_t bp1, sp1, bp2, sp2, bp3, sp3, bp4, sp4, bp5, sp5;
    
    int32_t tk, pp, ls, ft, v, ltq, tbq, tsq;
    int32_t bq1, sq1, bq2, sq2, bq3, sq3, bq4, sq4, bq5, sq5;
    int32_t bo1, so1, bo2, so2, bo3, so3, bo4, so4, bo5, so5;
    int32_t toi, oi, poi;
};
#pragma pack(pop)

struct DataHeader {
    uint32_t magic;
    uint16_t version;
    uint64_t total_messages;
    uint64_t total_batches;
    uint64_t first_timestamp;
    uint64_t last_timestamp;
    uint32_t symbol_count;
    char reserved[32];
};

// ========================================================================

std::mutex queue_mtx;
std::condition_variable queue_cv;

std::deque<std::vector<TickFields>> ready_batches;
bool done_read = false;
bool subscription_mode = false;

// ========================================================================

class SymbolMapper {
    public:
        void loadFromFile(std::ifstream& file) {
            uint32_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));

            for (uint32_t i=0; i<count; i++ ) {
                uint16_t id, len;
                file.read(reinterpret_cast<char*>(&id), sizeof(id));
                file.read(reinterpret_cast<char*>(&len), sizeof(len));
                std::string symbol(len, '\0');
                file.read(&symbol[0], len);
                idToSym_[id] = symbol;
            } 
        }

        std::string getSymbol(uint16_t id) {
            auto it = idToSym_.find(id);
            return (it != idToSym_.end()) ? it->second : "UNKNOWN";
        }
    
    private:
        std::unordered_map<uint16_t, std::string> idToSym_;
};

// ========================================================================

SymbolMapper symbol_mapper;

// ========================================================================

class ClientSession : public std::enable_shared_from_this<ClientSession> {
    public:
        ClientSession(websocket::stream<tcp::socket> ws, const std::string format) 
            : ws_(std::move(ws)), format_(format) {
                static std::atomic<uint8_t> nid{1};
                cid_ = nid.fetch_add(1);
                sam_.store(1, std::memory_order_relaxed);
                sm_.reset();
                fm_ = (format_ == "json") 
                    ? std::function<std::string(const TickFields&)>([this](const TickFields& t){return j_(t);}) 
                    : std::function<std::string(const TickFields&)>([this](const TickFields& t){return std::string(reinterpret_cast<const char*>(&t),sizeof(t));});
            }
                        
        uint8_t getClientId() const { return cid_; }        
        
        ALWAYS_INLINE bool isSubscribed(uint16_t tid) const {
            return sam_.load(std::memory_order_relaxed) | sm_[tid];
        }
    
        void subscribeTokens(const std::vector<uint16_t>& tids) {
            std::lock_guard<std::mutex> lk(smx_);
            sam_.store(0, std::memory_order_relaxed);
            for (auto t : tids) sm_.set(t);
            std::cout << "[Client :: " << +cid_ << "] Subscribed to " << tids.size() << " tokens. Total subscribed: " << sm_.size() << "\n";
        }
    
        void unsubscribeTokens(const std::vector<uint16_t>& tids) {
            std::lock_guard<std::mutex> lk(smx_);
            for (auto t : tids) sm_.reset(t);
            std::cout << "[Client :: " << +cid_ << "] Unsubscribed from " << tids.size() << " tokens. Total subscribed: " << sm_.size() << "\n";
        }
    
        void subscribeToAll() {
            std::lock_guard<std::mutex> lk(smx_);
            sam_.store(1, std::memory_order_relaxed);
            sm_.reset();
            std::cout << "[Client :: " << +cid_ << "] Subscribed to ALL tokens\n";
        }

        HOT_FUNCTION void sendTick(const TickFields& t) {
            ws_.write(net::buffer(fm_(t)));
        }
        
        websocket::stream<tcp::socket>& getWebSocket() { return ws_; }
    
    private:
        uint8_t cid_;
        websocket::stream<tcp::socket> ws_;
        std::string format_;
        std::atomic<uint8_t> sam_;
        std::bitset<65536> sm_;
        mutable std::mutex smx_;
        std::function<std::string(const TickFields&)> fm_;
        char jb_[2048];
    
        HOT_FUNCTION std::string j_(const TickFields& t) {
            char* p = jb_;
            *p++ = '{';
            
            #define P(n,f) {int r=sprintf(p,"\"" n "\":%.2f,",t.f/100.0);p+=(r*(t.f!=0));}
            #define I(n,f) {int r=sprintf(p,"\"" n "\":%d,",t.f);p+=(r*(t.f!=0));}
            
            P("lp",lp)P("ti",ti)P("pc",pc)P("o",o)P("h",h)P("l",l)P("c",c)P("ap",ap)P("uc",uc)P("lc",lc)P("52h",h52)P("52l",l52)
            P("bp1",bp1)P("sp1",sp1)P("bp2",bp2)P("sp2",sp2)P("bp3",bp3)P("sp3",sp3)P("bp4",bp4)P("sp4",sp4)P("bp5",bp5)P("sp5",sp5)
            I("tk",tk)I("pp",pp)I("ls",ls)I("ft",ft)I("v",v)I("ltq",ltq)I("tbq",tbq)I("tsq",tsq)
            I("bq1",bq1)I("sq1",sq1)I("bq2",bq2)I("sq2",sq2)I("bq3",bq3)I("sq3",sq3)I("bq4",bq4)I("sq4",sq4)I("bq5",bq5)I("sq5",sq5)
            I("bo1",bo1)I("so1",so1)I("bo2",bo2)I("so2",so2)I("bo3",bo3)I("so3",so3)I("bo4",bo4)I("so4",so4)I("bo5",bo5)I("so5",so5)
            I("toi",toi)I("oi",oi)I("poi",poi)
            
            #undef P
            #undef I
            
            p-=(*(p-1)==',');
            *p++='}';
            return std::string(jb_, p-jb_);
        }
};

// ========================================================================

class BroadcastManager {
    public:
        void addClient(std::shared_ptr<ClientSession> client) {
            std::lock_guard<std::mutex> lock(clients_mtx_);
            clients_.push_back(client);
            std::cout << "[Broadcast] Client added. Total clients: " << clients_.size() << "\n";
        }
        
        void broadcast(const TickFields& tick) {
            std::lock_guard<std::mutex> lock(clients_mtx_);
            
            auto it = clients_.begin();
            while (it != clients_.end()) {
                try {
                    (*it)->sendTick(tick);
                    ++it;
                } catch (const std::exception&) {
                    std::cout << "[Broadcast] Client :: " << +(*it)->getClientId() << " disconnected. Remaining: " << (clients_.size() - 1) << "\n";
                    it = clients_.erase(it);
                }
            }
        }
        
        size_t clientCount() const {
            std::lock_guard<std::mutex> lock(clients_mtx_);
            return clients_.size();
        }
        
        bool hasClients() const {
            return clientCount() > 0;
        }
    
    private:
        mutable std::mutex clients_mtx_;
        std::vector<std::shared_ptr<ClientSession>> clients_;
};

// ========================================================================

BroadcastManager broadcastManager;
std::mutex broadcast_start_mtx;
std::condition_variable broadcast_start_cv;
bool broadcasting_started = false;

// ========================================================================

void handleSubscriptionMessage(std::shared_ptr<ClientSession> client, const std::string& msg) {
    try {
        if (msg == "ping") {
            return;
        }
        
        simdjson::ondemand::parser parser;
        simdjson::padded_string json = simdjson::padded_string(msg);
        simdjson::ondemand::document doc = parser.iterate(json);
        
        std::string_view action = doc["action"].get_string();
        
        if (action == "subscribe") {
            auto tokens_field = doc["tokens"];
            std::vector<uint16_t> tokenIds;
            
            if (tokens_field.type() == simdjson::ondemand::json_type::array) {
                for (auto token : tokens_field) {
                    tokenIds.push_back(static_cast<uint16_t>(token.get_uint64().value()));
                }
                client->subscribeTokens(tokenIds);
            }
        } 
        else if (action == "unsubscribe") {
            auto tokens_field = doc["tokens"];
            std::vector<uint16_t> tokenIds;
            
            if (tokens_field.type() == simdjson::ondemand::json_type::array) {
                for (auto token : tokens_field) {
                    tokenIds.push_back(static_cast<uint16_t>(token.get_uint64().value()));
                }
                client->unsubscribeTokens(tokenIds);
            }
        }
        else {
            std::cout << "[Subscription] Unknown action: " << action << "\n";
        }
        
    } catch (const std::exception& e) {}
}

// ========================================================================

std::vector<TickFields> decompressBatch(const std::vector<char>& compressed) {
    std::vector<TickFields> ticks;
    
    size_t uncompressedSize = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (uncompressedSize == ZSTD_CONTENTSIZE_ERROR || 
        uncompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        return ticks;
    }
    
    std::vector<char> decompressed(uncompressedSize);
    size_t result = ZSTD_decompress(decompressed.data(), uncompressedSize,
                                    compressed.data(), compressed.size());
    
    if (ZSTD_isError(result)) {
        std::cerr << "Decompression error: " << ZSTD_getErrorName(result) << "\n";
        return ticks;
    }
    
    size_t tickCount = result / sizeof(TickFields);
    ticks.resize(tickCount);
    std::memcpy(ticks.data(), decompressed.data(), result);
    
    return ticks;
}

// ========================================================================

void producerThread(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);

    if (!in) {
        std::cerr << "Failed to open " << filename << "\n";
        return;
    }
    
    DataHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.magic != 0x5449434B) {
        std::cerr << "Invalid file format\n";
        return;
    }
    
    std::cout << "File info: " << header.total_messages << " ticks, "
              << header.total_batches << " batches, "
              << header.symbol_count << " symbols\n";
    
    symbol_mapper.loadFromFile(in);

    size_t symbolMapReservedSpace = 100000;
    in.seekg(sizeof(DataHeader) + symbolMapReservedSpace);

    uint64_t batchCount = 0;
    while (true) {
        uint32_t compressedSize;
        in.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));
        if (in.gcount() != sizeof(compressedSize)) break;
        
        std::vector<char> compressed(compressedSize);
        in.read(compressed.data(), compressedSize);
        if (in.gcount() != static_cast<std::streamsize>(compressedSize)) break;
        
        auto batch = decompressBatch(compressed);
        if (!batch.empty()) {
            std::unique_lock<std::mutex> lock(queue_mtx);
            ready_batches.push_back(std::move(batch));
            
            if (ready_batches.size() > 5) {
                queue_cv.wait(lock, []{ return ready_batches.size() <= 5; });
            }
            queue_cv.notify_all();
            
            batchCount++;
            if (batchCount % 10 == 0) {
                std::cout << "\r[Producer] Loaded " << batchCount << " batches" << std::flush;
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(queue_mtx);
    done_read = true;
    queue_cv.notify_all();
    
    std::cout << "\n[Producer] Done loading " << batchCount << " batches\n";
}

// ========================================================================

void streamMessages(double speed, uint32_t maxDelayUs) {
    uint64_t totalTicksStreamed = 0;
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        std::vector<TickFields> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            queue_cv.wait(lock, []{ return !ready_batches.empty() || done_read; });
            
            if (ready_batches.empty() && done_read) break;
            
            batch = std::move(ready_batches.front());
            ready_batches.pop_front();
            queue_cv.notify_all();
        }
        
        for (const auto& tick : batch) {
            while (!broadcastManager.hasClients()) {
                std::cout << "\r[Stream] Paused (no clients)..." << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (tick.timestamp_delta > 0) {
                uint32_t delayUs = (tick.timestamp_delta > maxDelayUs) ? maxDelayUs : tick.timestamp_delta;
                int64_t actualDelayUs = static_cast<int64_t>(delayUs / speed);
                std::this_thread::sleep_for(std::chrono::microseconds(actualDelayUs));
            }
            
            broadcastManager.broadcast(tick);
            totalTicksStreamed++;
            
            if (totalTicksStreamed % 10000 == 0) {
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                std::cout << "\r[Stream] Ticks: " << totalTicksStreamed 
                          << " | Clients: " << broadcastManager.clientCount()
                          << " | Elapsed: " << seconds << "s" << std::flush;
            }
        }
    }
    
    std::cout << "\n[Stream] Finished streaming " << totalTicksStreamed << " ticks\n";
}

// ========================================================================

void doSession(tcp::socket socket, const std::string& format) {

    std::shared_ptr<ClientSession> client;

    try {
        websocket::stream<tcp::socket> ws(std::move(socket));
        
        beast::websocket::stream_base::timeout opt{};
        opt.handshake_timeout = std::chrono::seconds(30);
        opt.idle_timeout = beast::websocket::stream_base::none();
        ws.set_option(opt);
        
        ws.accept();
        
        client = std::make_shared<ClientSession>(std::move(ws), format);

        std::cout << "\n[Session] Client :: " << +client->getClientId() << " connected\n";
        
        if (!subscription_mode) {
            client->subscribeToAll();
        }
        
        broadcastManager.addClient(client);
        
        {
            std::lock_guard<std::mutex> lock(broadcast_start_mtx);
            if (!broadcasting_started) {
                broadcasting_started = true;
                broadcast_start_cv.notify_all();
                std::cout << "[Session] First client connected, starting broadcast\n";
            }
        }
        
        while (true) {
            beast::flat_buffer buffer;
            client->getWebSocket().read(buffer);
            
            if (buffer.size() > 0) {
                std::string msg(static_cast<const char*>(buffer.data().data()), buffer.size());
                
                if (msg == "ping") {
                    client->getWebSocket().write(net::buffer(std::string("pong")));
                    continue;
                }
                
                if (subscription_mode) {
                    handleSubscriptionMessage(client, msg);
                }
            }
        }
        
    } catch (const std::exception& e) {
        std::cout << "\n[Session] Client :: " << +client->getClientId() << " disconnected: " << e.what() << "\n";
    }
}

// ========================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        
        std::cerr << "Usage: " << argv[0] << " -f <file.bin> [-s <speed>] "
                  << "[-fmt json|binary] [-p <port>] [-maxgap <ms>] [-sub] [-count]\n"
                  << "  -sub: Enable subscription mode (clients must subscribe to tokens)\n"   // This is incomplete.. needs modification
                  << "  -count: Just count ticks and exit (no streaming)\n";
        return 1;
    }
    
    std::string filename;
    double speed = 1.0;
    std::string format = "json";
    uint16_t port = 8080;
    uint32_t maxGapMs = 5000;
    bool countOnly = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-f" && i + 1 < argc) filename = argv[++i];
        else if (arg == "-s" && i + 1 < argc) speed = std::stod(argv[++i]);
        else if (arg == "-fmt" && i + 1 < argc) format = argv[++i];
        else if (arg == "-p" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "-maxgap" && i + 1 < argc) maxGapMs = std::stoi(argv[++i]);
        else if (arg == "-sub") subscription_mode = true;
        else if (arg == "-count") countOnly = true;
    }
    
    if (filename.empty()) {
        std::cerr << "Error: No input file specified\n";
        return 1;
    }
    
    if (countOnly) {
        std::ifstream in(filename, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open " << filename << "\n";
            return 1;
        }
        
        DataHeader header;
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        
        if (header.magic != 0x5449434B) {
            std::cerr << "Invalid file format\n";
            return 1;
        }
        
        std::cout << "\n=== FILE VERIFICATION ===\n";
        std::cout << "File: " << filename << "\n";
        std::cout << "Magic: 0x" << std::hex << header.magic << std::dec << " (OK)\n";
        std::cout << "Version: " << header.version << "\n";
        std::cout << "Total ticks (from header): " << header.total_messages << "\n";
        std::cout << "Total batches (from header): " << header.total_batches << "\n";
        std::cout << "Symbols: " << header.symbol_count << "\n";
        
        symbol_mapper.loadFromFile(in);
        std::cout << "Symbol map loaded: " << header.symbol_count << " symbols\n";
        
        size_t symbolMapReservedSpace = 100000;
        in.seekg(sizeof(DataHeader) + symbolMapReservedSpace);
        
        std::cout << "Seeking to batch data at position: " 
                  << (sizeof(DataHeader) + symbolMapReservedSpace) << "\n";
        
        uint64_t actualTickCount = 0;
        uint64_t actualBatchCount = 0;
        
        std::cout << "\nCounting ticks in batches...\n";
        
        while (true) {
            uint32_t compressedSize;
            in.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));
            if (in.gcount() != sizeof(compressedSize)) break;
            
            std::vector<char> compressed(compressedSize);
            in.read(compressed.data(), compressedSize);
            if (in.gcount() != static_cast<std::streamsize>(compressedSize)) break;
            
            auto batch = decompressBatch(compressed);
            actualTickCount += batch.size();
            actualBatchCount++;
            
            if (actualBatchCount % 50 == 0) {
                std::cout << "\r[Verify] Batches: " << actualBatchCount << " | Ticks: " << actualTickCount << std::flush;
            }
        }

        std::cout << "\r[Verified] Total Batches: " << actualBatchCount << " | Total Ticks: " << actualTickCount << "\n";
        
        std::cout << "\n\n=== VERIFICATION RESULT ===\n";
        std::cout << "Expected ticks:  " << header.total_messages << "\n";
        std::cout << "Actual ticks:    " << actualTickCount << "\n";
        std::cout << "Expected batches: " << header.total_batches << "\n";
        std::cout << "Actual batches:   " << actualBatchCount << "\n";
        
        if (actualTickCount == header.total_messages && 
            actualBatchCount == header.total_batches) {
            std::cout << "\nFILE VERIFIED: All ticks present!\n";
            return 0;
        } else {
            std::cout << "\nERR :: FILE CORRUPTED: Tick/batch count mismatch!\n";
            return 1;
        }
    }
    
    std::cout << "Config: speed=" << speed << "x, format=" << format 
              << ", port=" << port 
              << ", subscription_mode=" << (subscription_mode ? "ON" : "OFF") << "\n";
    
    std::thread(producerThread, filename).detach();
    uint32_t maxDelayUs = maxGapMs * 1000;
    
    std::thread([speed, maxDelayUs]() {
        {
            std::unique_lock<std::mutex> lock(broadcast_start_mtx);
            broadcast_start_cv.wait(lock, []{ return broadcasting_started; });
        }
        std::cout << "[Broadcaster] Starting stream...\n";
        streamMessages(speed, maxDelayUs);
    }).detach();
    
    try {
        net::io_context ioc;
        tcp::acceptor acceptor(ioc, {tcp::v4(), port});
        std::cout << "[Server] Started at ws://localhost:" << port << "\n";
        
        while (true) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            std::thread(doSession, std::move(socket), format).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "[Server] Fatal error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
