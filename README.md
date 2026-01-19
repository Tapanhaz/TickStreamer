## For Replaying market ticks ::

- Download the appropriate binary for your architecture from the
  [Releases](https://github.com/Tapanhaz/TickStreamer/releases) section.
- Run

```shell
./stream_ticks -f file.bin -s 10 -fmt json
```

It will stream the ticks at 10x speed (adjustable, default is normal)

- Now run the example_client.py in another shell.
