# CKPool - AtlasPool Edition

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

A production-ready, enhanced version of CKPool (by Con Kolivas) customized for AtlasPool's Bitcoin mining infrastructure. AtlasPool is a global solo mining pool with endpoints across North America, South America, Europe, Asia, and Australia, designed to provide low-latency connections and high reliability for solo Bitcoin miners worldwide.

## About

CKPool is an ultra low overhead, massively scalable, multi-process, multi-threaded modular Bitcoin mining pool server written in C for Linux. This repository contains AtlasPool's enhanced version with additional features and improvements for production mining operations.

**Original CKPool:** https://bitbucket.org/ckolivas/ckpool/src/master/

## Key Enhancements

### 🚀 HTTP REST API
- Built-in HTTP server on port 8080
- Real-time pool and user statistics
- JSON response format

### 🛡️ Security & Stability
- Buffer overflow fixes for HTTP API with large user datasets
- Case-insensitive Bitcoin address lookups
- Memory-safe string operations
- Dynamic memory allocation with safety limits

### ⛏️ Mining Compatibility
- **Rental Rig Support** - Compatible with MiningRigRentals and NiceHash
- **Modern ASIC Support** - Handles extranonce.subscribe requests
- **High Difficulty Ports** - Configurable minimum difficulty enforcement
- **User Agent Tracking** - Monitor miner software versions

### 🎯 Customization
- Custom block signatures (no hardcoded prefixes)
- Configurable difficulty parameters
- Flexible port configuration

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential yasm libzmq3-dev autoconf automake libtool
```

### Configuration

Create `ckpool.conf`:

```json
{
  # ... your regular ckpool configuration ...
  # See original README for complete options
  
  # AtlasPool fork enhancements:
  "btcsig": "/YourPoolName/",
  "highdiff": 1000000,
  "highdiffmin": 1000000
}
```

**Solo Mining Mode:** This code is configured for solo mining. The donation address is set to AtlasPool's wallet by default. To use your own wallet for fee collection, modify line 1758 in `src/ckpool.c`:

```c
ckp.donaddress = "your_bitcoin_address_here";
```

**AtlasPool-Specific Parameters:**

| Parameter | Description | Default |
|-----------|-------------|---------|
| `highdiff` | Starting difficulty for high diff ports (>4000) | 1000000 |
| `highdiffmin` | Minimum difficulty for high diff ports (>4000) | 1000000 |
| `btcsig` | Custom block signature (no prefix added) | - |

See [original CKPool documentation](README) for complete configuration options.

### Running

```bash
# Start the pool in solo mode
./src/ckpool -B -q -c /path/to/ckpool.conf

# Check HTTP API
curl http://localhost:8080/
```

## HTTP API Endpoints

```bash
# API version and status
GET /

# Pool statistics
GET /api/pool

# All user statistics
GET /api/users

# Specific user statistics
GET /api/users/{bitcoin_address}
```

**Example Response:**
```bash
$ curl http://localhost:8080/
{"name":"CKPool API Server","version":"1.0.1-patched","endpoints":["/api/status","/api/pool","/api/users","/api/users/{address}"]}
```

## Documentation

- **[ATLASPOOL_MODIFICATIONS.md](ATLASPOOL_MODIFICATIONS.md)** - Complete technical documentation of all modifications
- **[README](README)** - Original CKPool documentation
- **[README-SOLOMINING](README-SOLOMINING)** - Solo mining setup guide

## Building from Source

```bash
# Clone the repository
git clone https://github.com/atlaspool/ckpool-atlaspool.git
cd ckpool-atlaspool

# Generate configure script and build
./autogen.sh
./configure
make

# Binaries will be in src/ directory:
# - ckpool (main pool server)
# - ckpmsg (messaging utility)
# - notifier (block notification handler)
```

## Differences from Original CKPool

1. **HTTP API Server** - New REST API for statistics (472 lines)
2. **Buffer Overflow Fixes** - Critical memory safety improvements
3. **Case-Insensitive Lookups** - Better Bitcoin address handling
4. **High Diff Enforcement** - Configurable minimum difficulty
5. **Rental Rig Support** - "login" method compatibility (MiningRigRentals, NiceHash)
6. **Extranonce Subscribe** - Modern ASIC compatibility
7. **Custom Signatures** - No hardcoded "ckpool" prefix
8. **User Agent Tracking** - Miner software monitoring with automatic updates

See [ATLASPOOL_MODIFICATIONS.md](ATLASPOOL_MODIFICATIONS.md) for complete technical details.

## License

GNU General Public License v3.0 - See [COPYING](COPYING) for details.

**Original Author:** Con Kolivas  
**Modifications:** © 2025 AtlasPool

## Contributing

This is a production fork maintained by AtlasPool. For issues with:
- **AtlasPool modifications:** Open an issue in this repository
- **Original CKPool functionality:** See https://bitbucket.org/ckolivas/ckpool/

## Support

- **Documentation:** See docs in this repository
- **Original CKPool:** https://bitbucket.org/ckolivas/ckpool/
- **AtlasPool:** https://atlaspool.io

## Acknowledgments

Special thanks to Con Kolivas for creating CKPool, the foundation of this enhanced version.

---

**Status:** Production Ready | **Version:** Based on CKPool master (October 2025) + AtlasPool enhancements
