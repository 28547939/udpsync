
# udpsync

`udpsync` is a simple utility to send the contents of a file over a network when there
is only one-way communication available; UDP is used because responses are not available.
The utility can be seen as a "network `dd`" with reduced functionality but with the
addition of settings concerning retransmission, for example.

Data is transmitted in 512-byte "blocks", with two copies of each block being transmitted 
in each packet for redundancy (specifically to detect, but not correct errors).
So each packet's buffer is 1024 bytes, which is assumed to be small enough for the local/path MTU.
Reliable transmission is further achieved by retransmitting blocks a fixed number
of times (since no feedback on delivery is available from the remote endpoint).
Blocks are transmitted as part of "ranges" of blocks, with each range being typically 
no more than a few hundred or 1-2 thousand blocks.
Retransmission takes place on the level of ranges; ranges, not individual blocks are 
retransmitted.

Previously, the receiving side wrote to disk all retransmitted copies of a block (retransmitted
as part of a retransmitted range), which means that the last transmission, even if corrupt, 
would overwrite all the others.
This is the rationale behind the updated method, of sending two copies of the same 512-byte block in 
each 1024-byte buffer and confirming that they match before writing to disk, since otherwise there 
would be no protection against corrupted blocks within the final retransmission(s) of a range.

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
So to be lost, a block will have to be part of a packet loss cluster on every retransmission.
(One way this can happen is if there's an extended period of packet loss)

### Other notes

* There is a maximum of `2^32` blocks per file (1TB)
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

### Testing

The `small-test.sh` script creates a 1MB file and tests the sender and receiver locally (checking
that the received data is identical to the sent data). The test data generation function uses the 
same block of ASCII text repeatedly, and it's designed to be very clear when and where data has been 
lost or corrupted.
