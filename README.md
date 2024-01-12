
# udpsync

`udpsync` is a simple utility to send the contents of a file over a network when there
is only one-way communication available; UDP is used because responses are not available.
The utility can be seen as a "network <pre>dd</pre>" with reduced functionality but with the
addition of settings concerning retransmission, for example.

Data is transmitted in 1024-byte "blocks", which is assumed to be adequately small for the 
local/path MTU. Reliable transmission is achieved by retransmitting blocks a fixed number
of times (since no feedback on delivery is available from the remote endpoint).
Blocks are transmitted as part of "ranges" of blocks, with each range being typically 
no more than a few hundred or 1-2 thousand blocks.

### Options

The user adjusts several options to accommodate however lossy the network conditions may be.
* `--range-size`: How many blocks are in a range (default 128)
* `--total-retransmits`: How many times to retransmit each range (default 0)
* `--delay-usec`: How quickly to send - how long (microseconds) to wait between sending packets
    (default 1000)

#### Adjusting the options

Generally, the more lossy the network, the larger the `range-size`, `total-retransmits`, and
`delay-usec` will need to be. Since packet loss is generally bursty, a large `range-size` results
in a lower likelihood that any given block will be lost (not received in any of the retransmits),
since clusters of lost packets will be randomly distributed across the range with each retransmission.

### Other notes

* There is a maximum of `2^32` blocks per file (1TB)
* Using `delay-usec` instead of providing a limit on bandwidth (not yet implemented) allows
    the user to more closely control the sender's packet-per-second rate.
* Upon completion, the receiving side will scan for blocks that it did not receive and print
    out a list. This can be used to manually repair the received file.

#### TODO

* Eliminate re-writing of blocks that have already been received/written
* Calculate missing blocks even when killed

### Example

Sending side - difficult network, very slow transmission
```
# ./udpsync --address $ADDRESS --port $PORT --path $SRCPATH \
    --total-retransmits 4 --range-size 512 --delay-usec 50
```

Receiving side
```
# ./udpsync --receive --address $ADDRESS --port $PORT --path $DSTPATH

```
