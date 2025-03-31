Use cache side channels to share text message between two processes. These
processes don't communicate through any other means. There is a sender and a
receiver in each case. Build and run those executables in different terminals
at the same time, further instructions will appear. Also see "Before Running" and
"Note" below.

## Note
- If on laptop then put it in performance mode(`/etc/default/cpupower`), keep
  it plugged in.
- Don't run browser or any other demanding app in parallel, test it on low
  noise system.
- Set cpu affinities for both sender and receiver using `taskset`. 
  `taskset -c 0 ./sender` & `taskset -c 4 ./receiver`
- Although these cache attacks are targeting L3 cache but this POC is working
  with L2 cache. It took some time to realize why sender & receiver need to be
  taskset'd to 0 and 4th. They are the physical indices of the hyperthread
  siblings which lie on the same core. Run `lstopo` to see the physical
  index(`P#x`) of the threads which exist on the same physical core.  Because
  they need to exist on the same physical core, they are using that core's L2
  cache. But in `sender.c` and `receiver.c` files I had always assumed that its
  using L3 cache, I have not corrected those.

# Flush+Reload
[Flush+Reload: A High Resolution, Low Noise, L3 Cache Side-Channel
Attack](https://www.usenix.org/system/files/conference/usenixsecurity14/sec14-paper-yarom.pdf)

Share 'msg.txt' file, bit by bit. Each bit is shared by maintaining a
particular cache state for a fixed amount of time. For redundancy, each bit is
shared multiple times. `libm.so.6` is dynamically loaded at runtime and its
`sin` and `sqrt` function pointers are used to communicate. This is how the
cached/no-cached state of these pointers is interpreted:

    sin cached, sqrt cached => Initiate listening. Receiver waits for this
                               signal and once the sender is done, this 
                               same signal is used again to show end 
                               of message.

    sin cached, sqrt not-cached => bit 1
    sin not cached, sqrt cached => bit 0

    both not cached => garbage, retry.

## Results
- 330 bits/sec with 100% accuracy.

## Before Running
- Make `calibration` and run it, that gives the threshold for cache hit. Update
  `HIT_THRESHOLD` in `receiver.c`
- Make sure `/usr/lib/libm.so.6` exists. If you have a different version or a
  different path for libm then update it in `utils.h` file.
- Message filename 'msg.txt' must be text only.

# Cache Occupancy
[Robust Website Fingerprinting Through the
Cache Occupancy Channel](https://www.usenix.org/system/files/sec19-shusterman.pdf)

Receiver has an array that can fit in L3 cache. It does random accesses in this
array and counts how many accesses it could make in a fixed time interval. To
send 0 the sender thrashes the cache, to send 1 it just sleeps. This state is
maintained for a fixed amount of time.  Depending on whether the cache is being
thrashed or not, the receiver will observe that its counter values are
clustered around 2 values, high and low.  There is a constant
`COUNTER_THRESHOLD` which is close to mid between the high and low counter values.
You may need to perturb it a little bit, up or down, to get 100% accuracy.
Run `receiver.cal` & `sender.cal` from `./cache-occupancy/occupancy_calibration/`
to find threshold value for your machine. 

By design, the message in 'msg.txt' must begin with either 'Hi' or 'HI'.
If thats not the case then the 0% accuracy is most likely outcome.

In the occupancy-calibration the `sender.cal` and `receiver.cal` just thrash and
observer the cache. The receiver will print the counter values and you will be
able to notice that they clump close to a high and a low value. Choose the
counter threshold to be somewhere in the middle with less chances of false
positives. On my machine this value was 125000. Dont forget to update it in
`receiver.c`

To transmit each bit we need to maintain a particular cache state for a brief
period. To add redundancy we are transmitting each bit multiple
times. In my case, I am transmitting each bit 8 times and maintaining the state
corresponding to a particular transmission for 8ms. Unlike flush+reload it does not
recreates the transmitted message in realtime. The receiver takes the 
long transmission with redundancies and decodes it in the end.

See `receiver.c:248` comment to understand why msg.txt must begin with 'Hi'/'HI'

## Results
- 15.8 bits/sec with 100% accuracy.

## Before Running
- The transmission alphabet is limited. See `VALID_CHARS` in `receiver.c` and
  update it if you need more.
- Message filename 'msg.txt' must be text only.

# FlushReload+CacheOccupancy (SENDER aka SIMRAN, RECEIVER aka RAJ)
First SENDER shares her contact address(library, function names) with RECEIVER
using Cache Occupancy, then RECEIVER uses that address to share a file with
SENDER using flush-reload. File is not expected to be a text message, it could
also be an image, audio etc. This is not the case with previous two communications.

See `sender.c` to understand the format of her contact address. It needs a
library name and two functions from that library that take and return `double`.
Once receiver receives the contact address he first sends the file size which
sender immediately decodes and then waits for that many bytes before saving the
file. 

## Results
- First 20sec are used to share the address, then the main file is shared at
  the rate 330 bits/sec. Accuracy 100%. 5.6Kb file takes about 2min33sec to transmit.

## Running
- update the name of the file you want to transmit in `utils.h`
- the transmitted file is saved to `from_raj_to_simran.bin`
- To try different contact addresses update it in `sender.c.` Make sure
  that you read and fullfill the requirements mentioned there in 
  the comments. Simran's address is in a very specific format and
  Raj expects it.
- Use the same threshold values as in F+R & CO cases.

# Prime+Probe (TODO)
[Last-Level Cache Side-Channel Attacks are
Practical](https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=7163050)

[Prime+Probe 1, JavaScript 0: Overcoming Browser-based Side-Channel
Defenses](https://arxiv.org/pdf/2103.04952)
