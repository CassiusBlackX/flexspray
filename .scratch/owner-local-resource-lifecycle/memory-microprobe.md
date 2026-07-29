# Jetty and TP connection memory microprobe

## Scope

This throwaway probe measures current retained host RSS for active empty entities without packets,
WQEs, routing, phase execution, or trace output. It uses production `UbJettyManager`,
`TpConnectionManager`, endpoint materialization,
`UbTransportChannel`, lifecycle coordinator, congestion-control, retransmission, and switch
allocator construction paths.

Source:

`scratch/ub-memory-microprobe.cc`

Build:

```sh
python3.12 ./ns3 build -j8 ub-memory-microprobe
```

Each point ran in a fresh process. Counts were visited from high to low and repeated five times.
Current resident bytes came from `mach_task_basic_info.resident_size` on macOS arm64.

## Results

| Mode | Counts | Fitted slope | R-squared |
| --- | --- | ---: | ---: |
| Jetty | 0, 1,024, 4,096, 16,384, 65,536 | 17,254.45 B/Jetty | 0.9999999886 |
| Reservation | 0, 1,024, 4,096, 16,384, 65,536 | 1,197.32 B/connection | 0.9999086847 |
| Reservation plus two endpoints | 0, 256, 1,024, 4,096, 16,384 | 42,784.92 B/connection | 0.9999990741 |

High-count baseline-to-treatment slopes were even tighter:

- Jetty median `17,255.25 B`, range `17,254-17,256.5 B`.
- Reservation median `1,201.25 B`, range `1,200.75-1,202.25 B`.
- Reservation plus endpoints median `42,800 B`, range `42,798-42,801 B`.

Subtracting the reservation fit gives approximately `20,794 B` per materialized empty TP endpoint.
At those slopes, 1,024 active two-ended connections retain about `41.8 MiB`, while 10,000 retain
about `408 MiB`. Empty Jetties retain about `16.8 MiB` per 1,024 and `165 MiB` per 10,000.

## Median RSS By Count

| Mode | Count | Median current RSS |
| --- | ---: | ---: |
| Jetty | 0 | 9,289,728 B |
| Jetty | 1,024 | 27,033,600 B |
| Jetty | 4,096 | 80,084,992 B |
| Jetty | 16,384 | 292,093,952 B |
| Jetty | 65,536 | 1,140,146,176 B |
| Reservation | 0 | 9,224,192 B |
| Reservation | 1,024 | 10,682,368 B |
| Reservation | 4,096 | 14,729,216 B |
| Reservation | 16,384 | 29,179,904 B |
| Reservation | 65,536 | 87,965,696 B |
| Reservation plus endpoints | 0 | 9,437,184 B |
| Reservation plus endpoints | 256 | 20,692,992 B |
| Reservation plus endpoints | 1,024 | 53,657,600 B |
| Reservation plus endpoints | 4,096 | 185,450,496 B |
| Reservation plus endpoints | 16,384 | 710,639,616 B |

The reservation counts matched the production storage model exactly: each connection added one
canonical record, two local views, fourteen secondary-index entries, and no endpoint. Endpoint mode
added two resident TP endpoints per connection.

## Attribution

The release-build ABI inline sizes were:

- `sizeof(UbJetty) = 208 B`;
- `sizeof(Connection) = 48 B`;
- `sizeof(UbTransportChannel) = 768 B`.

Inline size is not total retained memory. `UbJetty::m_ssnAckWindow` and
`UbTransportChannel::m_recvPsnWindow` each initialize a `UbSlidingBitmapWindow` with 2,048
`uint64_t` slots. Each empty entity therefore allocates `16,384 B` of window backing immediately.
This explains about 95 percent of the empty-Jetty slope and about 79 percent of the endpoint-only
estimate. The rest includes manager and lifecycle map nodes, congestion-control and retransmission
objects, secondary indexes, callbacks, and allocator registration.

## Limits

- This is a retained-state microbenchmark, not a full workload peak-RSS measurement.
- The probe does not execute explicit destroy. It therefore does not measure owner-state removal,
  retained quarantine size, or RSS returned to the operating system after destroy.
- The slopes quantify occupied active state and bound growth that timely explicit destroy may avoid;
  actual process-RSS reduction also depends on allocator page retention.
- Empty entities exclude WQE, packet, queue, and active retransmission payloads, so active entities
  can cost more.
- Reservation slope includes the current duplicated local views and all current indexes. It should
  not be described as `sizeof(Connection)`.
- Results are build-, ABI-, allocator-, and platform-specific and should be refreshed after storage
  representation changes.
