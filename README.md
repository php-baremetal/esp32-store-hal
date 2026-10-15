# esp32-store-hal

The ESP32 side of [ext-store](package:php-baremetal/ext-store): it keeps the extension's key-value pairs in NVS
flash, in a partition of its own. Platform `esp32`; it provides the virtual `php-baremetal/store-hal`.

You do not require this package. Require `php-baremetal/ext-store`, and conductor adds `esp32-store-hal` when the
project's `extra.baremetal.sdk` is `esp32@^1.0`:

```bash
composer require php-baremetal/ext-store
```

## What it declares

| Item | Value |
| --- | --- |
| Type, platform | `bm-hal`, `esp32` |
| Provides | `php-baremetal/store-hal` |
| Requires | `php-baremetal/esp32-executor` |
| ESP-IDF component (`idf-requires`) | `nvs_flash` |
| Partition | `phpstore`, subtype `nvs`, 24K |

The build adds the `phpstore` partition to the board's partition table. There is no project setting for it.

## Behaviour

- **Storage.** NVS partition `phpstore`, namespace `php`. `store_clear()` erases that namespace only. Keys are
  capped at 15 bytes by ext-store (the NVS limit); a string value is capped by NVS at 4000 bytes with its
  terminator.
- **Everything runs on the executor.** The PHP task keeps its stack in PSRAM and must not touch flash, so each
  NVS call (open, read, write, commit, erase, key listing) is handed to the executor task of `esp32-executor` and
  the PHP task waits. A call that cannot be queued within 1000 ms fails.
- **Commit on write.** `set`, `delete` and `clear` call `nvs_commit()` before they return.
- **Boot.** The partition is initialised at PHP module start. When NVS reports no free pages or a newer format,
  the partition is erased and initialised again, so every key is lost. A successful open logs
  `persistent store ready (partition 'phpstore')`.
- **Key listing.** The keys are gathered on the executor into internal RAM (a buffer that doubles as it fills) and
  then returned to PHP, which receives them through a callback on the PHP task.

## Documentation

The PHP functions, limits and recipes are documented with the extension: see
[ext-store](package:php-baremetal/ext-store#docs/overview.md) and its
[limits and durability](package:php-baremetal/ext-store#docs/reference/limits.md). The `executor_run_sync()` API
is described in [esp32-executor](package:php-baremetal/esp32-executor).

## License

MIT.
