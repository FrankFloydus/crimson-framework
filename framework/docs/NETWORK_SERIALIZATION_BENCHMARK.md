<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Network serialization benchmark

Use the benchmark to measure how much CPU time and packet space a serialization change consumes. Run it from the workspace root in PowerShell 7 with an installed Release addon, Visual Studio's x64 C++ tools, and the Godot 4.7 console executable.

```powershell
$GodotExe = 'C:\Godot\Godot_v4.7-stable_win64_console.exe'
& .\framework\scripts\Test-NetworkSerialization.ps1 -GodotExe $GodotExe
```

The harness prints a fresh artifact directory with `results.json`, native compiler and run logs, and Godot RPC results. It records the CPU name, Godot build, compiler flags, and Release addon hash. Pass `-Recipients 1,8,32,128,256` to choose the fanout sizes, or `-BinaryDirectory` to select a different Release addon. [Benchmark harness](../scripts/Test-NetworkSerialization.ps1)

## Workloads and measurement boundaries

- **Native movement codec.** An optimized MSVC `/O2 /DNDEBUG` executable calls the same C++ row functions as the movement integration. It verifies every value of a round trip before measuring. It warms up and takes 20 samples of 40,000 packets per operation, cycling through 64 inputs. It reports elapsed time and Windows thread CPU cycles for encode and decode separately. The allocation figure counts ordinary C++ `new` and `new[]` calls during a correctness pass. The caller supplies the buffers and decoded rows. This figure excludes the Godot `PackedByteArray`, RPC, and transport allocations. [Native fixture](../scripts/fixtures/network_serialization_benchmark.cpp)
- **Godot movement RPC.** Real `SceneMultiplayer` encodes the numeric array arguments and the current byte array arguments. Both pose formats include the same fields. The current input includes jump and lock-on heading in addition to the old axes and aim fields. A test peer captures each outgoing packet and delivers it in memory. The harness verifies the receiver's decoded arguments, warms the path cache, then alternates formats across 21 timed samples. Send time includes RPC encoding and enqueue into the test peer. Receive time is the sum of the guest polls and includes engine decoding and dispatch. The byte payload is built before timing. These timings exclude codec preparation, actual sockets, movement simulation, and transport headers. [RPC fixture](../project/tests/replication/network_serialization_benchmark.gd)
- **Committed state.** The same packet harness measures the production `StateReplication` service with 128 visible records per guest and one changed record per commit. After every guest acknowledges its baseline, the harness verifies that each timed commit emits one update per guest and that the guests apply the new revision. Server time includes projection, validation, copying, diffing, RPC encoding, and enqueue. Each guest polls in sequence, so aggregate receive time shows total work across guests. It does not represent one guest's frame cost. [State replication](../src/modules/network/README.md)

Median and p95 values summarize sample averages. They do not measure the tail latency of an individual packet.

## Windows Release measurements from the initial codec

These results came from an AMD Ryzen 9 7900X with Godot 4.7 stable `5b4e0cb0f`. Each native packet contains 24 rows.

| Native operation | Payload | Median | p95 | Median thread cycles | Tracked C++ allocations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Input encode | 384 bytes | 119 ns | 126 ns | 558 | 0 |
| Input decode | 384 bytes | 230 ns | 271 ns | 1,030 | 0 |
| Pose encode | 576 bytes | 215 ns | 258 ns | 961 | 0 |
| Pose decode | 576 bytes | 224 ns | 259 ns | 1,032 | 0 |

A warmed input RPC measured **403 bytes** for both formats. A 24-actor pose RPC measured **603 bytes** with the original arrays and **595 bytes** with the new codec. The row data still uses full-width values. The eight-byte saving comes from sending one packed array instead of two.

| Recipients | Pose send median, original to codec | Pose send p95, original to codec | Aggregate pose receive median, original to codec | Committed state server median / p95 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 2.75 to 2.63 µs | 2.94 to 2.81 µs | 1.56 to 1.44 µs | 0.13 / 0.15 ms |
| 8 | 22.25 to 21.00 µs | 29.94 to 32.75 µs | 12.69 to 11.56 µs | 1.45 / 1.92 ms |
| 32 | 95.25 to 85.50 µs | 113.81 to 116.13 µs | 53.06 to 47.63 µs | 8.49 / 9.94 ms |
| 128 | 502.25 to 474.56 µs | 564.88 to 602.25 µs | 272.44 to 253.81 µs | 45.96 / 49.59 ms |
| 256 | 1,410.56 to 1,311.19 µs | 1,954.88 to 1,833.25 µs | 778.06 to 658.94 µs | 95.32 / 113.19 ms |

The codec saves roughly 1.3% per full pose RPC in this workload. A repeat with the installed protocol 2 Release addon reproduced the packet sizes and measured about 1.19 to 1.17 ms median RPC send time at 256 recipients. At 32 recipients the median varied from a decrease of 9.75 µs in the table to an increase of 1.94 µs in that repeat. At 256 recipients, the committed-state service spent about 80–97 ms across runs publishing a single changed record, even though each recipient received only about 144 RPC bytes. The committed-state code is identical across those runs. It processes each complete visible view before sending the small delta.

## Input with jump in protocol 3

The 24-command input payload in protocol 3 was 408 bytes. Its warmed Godot RPC was 427 bytes. The additional 24 bytes carried one jump flag per tick. The codec measured 129 ns median and 146 ns p95 to encode the payload, and 351 ns median and 400 ns p95 to decode it, with zero tracked C++ allocations. Pose framing remained 595 bytes per full RPC. At 256 simulated recipients the pose RPC took 1.25 ms median to send and 0.65 ms median to receive in aggregate.

## Controller headings in protocol 4

The 24-row input payload is 528 bytes. Its new fields indicate whether lock-on is active and supply the steering heading. The 24-row pose payload is 672 bytes and includes authority body yaw alongside the view yaw and pitch.

With the same Windows Release harness, the warmed input and pose RPCs measured 547 and 691 bytes. Median native encode and decode times were 198 and 269 ns for the input, and 255 and 270 ns for the pose, with zero tracked C++ allocations. At 256 simulated recipients the pose RPC took 1.19 ms median to send and 0.61 ms median to receive in aggregate.

## Live ENet scale regression

The [movement scale fixture](../project/tests/replication/movement_scale.gd) creates one dedicated authority and 32 guests with separate multiplayer and physics scopes in a single headless process. The endpoints use actual loopback ENet sockets. Each guest moves through its authored input, the authority acknowledges those inputs, and every guest's copy of every actor must converge to the authority within the fixture tolerance. The regular [multiplayer harness](../scripts/Test-Multiplayer.ps1) runs this fixture for Debug and Release. It reports admission and convergence wall times separately from the serializer measurements.

The 32-guest run exposed stale rosters on both the pre-codec and first codec builds. Godot retains the dictionary passed when registering an RPC. The movement setup reused and mutated it, causing the reliable roster to inherit the input RPC's unreliable mode. The roster now has a separate configuration. Final Debug and Release runs verified all 1,024 guest-to-actor views at 32 guests. [Movement delivery contract](MOVEMENT_NETWORKING.md#prediction-and-reconciliation) · [Godot RPC registration source](https://github.com/godotengine/godot/blob/4.7-stable/scene/main/node.cpp#L803-L814)

Additional 64-guest Debug and Release runs each verified all 4,096 views within 0.10 m of the authority. In Release, admission took 5.43 seconds, the 45 input ticks took 2.11 seconds of wall time, and positions converged 1.43 seconds after input release. The same fixture accepts `-- 64` after its script path for this workload. It measures correctness under load. Its tick duration reflects the work of running the authority, guests, and their physics together in one process.
