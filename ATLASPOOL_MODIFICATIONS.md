# AtlasPool Modifications to CKPool

**Original Source:** https://bitbucket.org/ckolivas/ckpool/src/master/  
**Cloned:** October 2025  
**Modified By:** AtlasPool Development Team  
**Last Updated:** January 13, 2026

---

## Overview

This document details all modifications made to the original CKPool stratum server to create a customized version for AtlasPool. The modified code is maintained in this repository as `ckpool-atlaspool`.

---

## Summary of Modifications

AtlasPool has made **8 major modifications** to the original CKPool codebase:

1. **HTTP API Server** - Added REST API endpoint on port 8080
2. **Buffer Overflow Fix** - Fixed critical memory safety issues in HTTP API
3. **Case-Insensitive Wallet Lookup** - Support for mixed-case Bitcoin addresses
4. **High Difficulty Port Minimum** - Enforce minimum difficulty on high-diff ports
5. **Rental Rig "login" Method** - Support for MiningRigRentals and NiceHash compatibility
6. **Extranonce Subscribe Support** - Dummy response for modern ASIC compatibility
7. **BTC Signature Customization** - Removed hardcoded "ckpool" prefix from blocks
8. **User Agent Tracking** - Track and store miner software information with automatic updates

---

## 1. HTTP API Server (Port 8080)

### Purpose
Provide a lightweight HTTP REST API for retrieving pool statistics without requiring direct database access or complex authentication.

### Implementation

**New Files Created:**
- `src/api_server.c` (472 lines) - HTTP server implementation
- `src/api_server.h` (30 lines) - API header definitions

**Modified Files:**
- `src/ckpool.c` - Initialize and start API server
- `src/Makefile.am` - Add api_server to build

### API Endpoints

```
GET /                           - API version and status
GET /api/users                  - All user statistics
GET /api/users/{address}        - Single user statistics
```

### Features
- Non-blocking HTTP server on port 8080
- Reads user statistics from ckpool log files
- JSON response format
- Graceful degradation (pool continues if API fails)
- Dynamic memory allocation with safety limits

### Configuration
No configuration required - automatically starts on port 8080 when ckpool launches.

**Code Changes:**
```c
// src/ckpool.c - lines 1903-1913
/* Start HTTP API server on port 8080 - non-critical, continue if fails */
api_server_set_log_dir(ckp.logdir);
if (api_server_init(8080) == 0) {
    LOGNOTICE("HTTP API server started on port 8080");
} else {
    LOGWARNING("Failed to start HTTP API server, continuing without it");
}
```

---

## 2. Buffer Overflow Fix (December 14, 2025)

### Problem
The HTTP API had critical buffer overflow vulnerabilities when handling large user statistics files:
- Fixed 65KB buffer for ALL users combined
- Unsafe `strcat()` without bounds checking
- No file size validation

### Trigger
Large user statistics files with billions of shares and long worker names caused "double free or corruption" crashes.

### Solution

**Dynamic Memory Allocation:**
- Initial buffer: 128KB (grows as needed)
- Maximum response: 10MB
- Buffer automatically doubles when space runs low

**Safe String Operations:**
- Created `safe_append()` function with bounds checking
- Replaced all unsafe `strcat()` calls
- Tracks buffer size and usage at all times

**File Size Validation:**
- Maximum user file size: 64KB
- Uses `stat()` to check before reading
- Skips oversized files with warning

**Modified File:**
- `src/api_server.c` - Complete rewrite of memory handling

### Impact
- Zero crashes since deployment
- Handles users with billions of shares
- Graceful error handling instead of crashes

---

## 3. Case-Insensitive Wallet Lookup (December 15, 2025)

### Problem
Bitcoin addresses are case-sensitive in Base58Check encoding, but:
- CKPool stores files with mixed-case addresses
- Users query with lowercase addresses
- File system lookups are case-sensitive on Linux
- Result: "User not found" errors for valid users

### Solution

**Added Case-Insensitive File Lookup:**
```c
/* Find user file with case-insensitive matching */
static char* find_user_file_case_insensitive(const char *address) {
    DIR *dir;
    struct dirent *entry;
    
    dir = opendir(users_dir);
    while ((entry = readdir(dir)) != NULL) {
        /* Case-insensitive comparison */
        if (strcasecmp(entry->d_name, address) == 0) {
            result = malloc(strlen(entry->d_name) + 1);
            strcpy(result, entry->d_name);
            break;
        }
    }
    closedir(dir);
    return result;
}
```

**Modified File:**
- `src/api_server.c` - Added case-insensitive lookup function

### Impact
- HTTP API works with any case variation of wallet address
- Users can query with lowercase, uppercase, or mixed case
- No more "User not found" errors for valid addresses

---

## 4. High Difficulty Port Minimum (December 29, 2025)

### Problem
High difficulty ports (port > 4000) start with `highdiff` (1,000,000) but vardiff can lower difficulty significantly, defeating the purpose of dedicated high-hashrate ports.

### Solution

**Added Configurable Minimum:**
- New config parameter: `highdiffmin` (default: 1,000,000)
- Enforces minimum difficulty floor on high diff ports
- Normal ports unaffected

**Modified Files:**
- `src/ckpool.h` (line 238) - Added `highdiffmin` field
- `src/ckpool.c` (lines 1477, 1787-1788) - Parse config and set default
- `src/stratifier.c` (lines 5707-5710) - Enforce minimum

**Code Changes:**
```c
// src/stratifier.c - lines 5707-5710
/* ATLASPOOL: Enforce minimum difficulty for high diff ports */
if (ckp->server_highdiff && ckp->server_highdiff[client->server]) {
    optimal = MAX(optimal, ckp->highdiffmin);
}
```

### Configuration

Add to `ckpool.conf` (optional):
```json
{
  "highdiff": 1000000,
  "highdiffmin": 1000000
}
```

**Configuration Parameters:**
- `highdiff` - Starting difficulty for high diff ports (>4000) - Default: 1,000,000
- `highdiffmin` - Minimum difficulty floor for high diff ports (>4000) - Default: 1,000,000

**Behavior:**
- High diff ports (> 4000): Start at `highdiff` and cannot drop below `highdiffmin`
- Normal ports (≤ 4000): Unaffected, vardiff works normally
- Both parameters configurable per pool instance

### Impact
- High diff ports maintain intended difficulty
- Prevents low-hashrate miners from overwhelming high ports
- Configurable for different pool needs

---

## 5. Rental Rig "login" Method (December 27, 2025)

### Problem
Rental mining services (MiningRigRentals.com and NiceHash) use non-standard `{"method": "login"}` instead of `{"method": "mining.authorize"}`, causing disconnections:
```
Dropping login from unsubscribed client
```

### Solution

**Added "login" Method Handler:**
Treats "login" as an alias for "mining.auth", using identical authorization flow. Supports rig rental options including both MiningRigRentals and NiceHash.

**Modified File:**
- `src/stratifier.c` (lines 6713-6733) - Added login handler

**Code Changes:**
```c
/* HANDLE RENTAL RIG "login" METHOD */
/* Some rental services use "login" instead of "mining.auth" */
if (cmdmatch(method, "login")) {
    json_params_t *jp;
    
    if (unlikely(client->authorised)) {
        LOGINFO("Client %s %s trying to authorise twice via login",
                client->identity, client->address);
        return;
    }
    
    LOGDEBUG("Client %s %s using 'login' method (rental rig)", 
             client->identity, client->address);
    
    /* Treat "login" as "mining.auth" */
    jp = create_json_params(client_id, method_val, params_val, id_val);
    ckmsgq_add(sdata->sauthq, jp);
    return;
}
```

### Impact
- Rental rigs from MiningRigRentals and NiceHash can now connect successfully
- Same authorization process as standard miners
- No security implications (same validation)
- Zero "Dropping login" errors
- Full compatibility with major rental platforms

---

## 6. Extranonce Subscribe Support (December 2025)

### Problem
Modern ASICs and rental services send `mining.extranonce.subscribe` requests. Original CKPool logs these as "Unhandled method" which can confuse monitoring systems.

### Solution

**Added Dummy Success Response:**
Sends a success response to acknowledge the request without implementing full extranonce subscription (not needed for pool operation).

**Modified File:**
- `src/stratifier.c` (lines 6782-6795) - Added extranonce handler

**Code Changes:**
```c
/* HANDLE RENTAL EXTRANONCE REQUESTS */
if (cmdmatch(method, "mining.extranonce.subscribe")) {
    /* AtlasPool Fix: Send dummy success to modern ASICs */
    json_t *val = json_object();
    json_object_set_nocheck(val, "result", json_true());
    json_object_set_nocheck(val, "id", id_val);
    json_object_set_new_nocheck(val, "error", json_null());
    
    /* Use internal stratum_add_send to handle the transmission and cleanup */
    stratum_add_send(sdata, val, client_id, SM_SUBSCRIBERESULT);
    return;
}
```

### Impact
- Modern ASICs connect without warnings
- Cleaner logs (no "Unhandled method" messages)
- Better compatibility with rental services
- No functional change to mining operation

---

## 7. BTC Signature Customization (December 15, 2025)

### Problem
Original CKPool hardcodes "ckpool" prefix in block coinbase signature, preventing pool operators from having full control over their block signature.

**User Config:** `"btcsig": "/AtlasPool-BOM/"`  
**Actual Result:** Block signature was `ckpool/AtlasPool-BOM/`

### Solution

**Removed Hardcoded Prefix:**
Changed line 578-579 in stratifier.c to start with empty signature, using only user-defined `btcsig` config.

**Modified File:**
- `src/stratifier.c` (lines 575-581) - Removed hardcoded prefix

**Code Changes:**
```c
// BEFORE:
wb->coinb2bin = ckzalloc(512);
memcpy(wb->coinb2bin, "\x0a\x63\x6b\x70\x6f\x6f\x6c", 7);  // "ckpool"
wb->coinb2len = 7;

// AFTER:
wb->coinb2bin = ckzalloc(512);
/* Remove "ckpool" prepend from btcsig */
wb->coinb2len = 0;  // Start with empty signature
```

### Impact
- Block signature is exactly what's configured in `btcsig`
- Full control over coinbase signature
- AtlasPool branding in blocks instead of "ckpool"

**Result:** Block signature is now `/AtlasPool-BOM/` (or whatever is configured)

---

## 8. User Agent Tracking (December 2025)

### Purpose
Track miner software (user agent) information for analytics and support purposes.

### Implementation

**Modified Files:**
- `src/stratifier.c` (line 173) - Added `useragent` field to worker_instance
- `src/stratifier.c` (lines 5378-5385) - Update worker useragent from client
- `src/stratifier.c` (lines 8134-8135) - Include useragent in worker stats

**Code Changes:**
```c
// Added to worker_instance struct:
char *useragent;

// Update worker useragent on every connection:
if (client->useragent && strlen(client->useragent) > 0) {
    if (worker->useragent) {
        free(worker->useragent);
    }
    worker->useragent = strdup(client->useragent);
}

// Include in worker statistics:
JSON_CPACK(wval, "{ss,ss,ss,ss,ss,ss,ss,si,sI,sf,sI}",
    "workername", worker->workername,
    "useragent", worker->useragent ? worker->useragent : "",
    // ... other fields
```

### Bug Fix (January 2026)
**Issue:** User agents were not updating when miners upgraded firmware. The original code only set the user agent on first connection, never updating it on subsequent reconnections.

**Fix:** Changed logic to always update user agent on every connection, ensuring dashboard always shows current miner software version.

### Impact
- Worker statistics now include miner software information
- User agents update automatically when miners upgrade
- Helps identify problematic miner versions
- Useful for support and analytics

---

## Configuration Changes

### Default Donation Address

**Modified File:** `src/ckpool.c` (line 1758)

**Change:**
```c
// Updated to AtlasPool address for solo mining fee collection:
ckp.donaddress = "bc1q4mtk3hsnlfhh8475krdnf4hec86ue43eckywft";
```

This sets the default donation address to AtlasPool's address. When running in solo mining mode (`-B` flag), this address receives the configured donation percentage from solved blocks.

**To use your own address:** Modify line 1758 in `src/ckpool.c` before compiling:
```c
ckp.donaddress = "your_bitcoin_address_here";
```

---

## Build System Changes

### Makefile Modifications

**Modified File:** `src/Makefile.am`

**Added:**
- `api_server.c` to source files
- `api_server.h` to header files
- Link against pthread library for HTTP server

---

## File Summary

### New Files Created
```
src/api_server.c          - HTTP API server implementation (472 lines)
src/api_server.h          - API header definitions (30 lines)
```

### Modified Files
```
src/ckpool.c              - API initialization, config parsing, donation address
src/ckpool.h              - Added highdiffmin field
src/stratifier.c          - 7 modifications (login, extranonce, btcsig, highdiff, useragent)
src/Makefile.am           - Build system changes
```

---

## Testing and Deployment

### Compilation
```bash
./configure
make clean
make
```

### Verification
```bash
# Check HTTP API
curl http://localhost:8080/

# Check ckpool version
./src/ckpool --help
```

---

## Security Considerations

### Memory Safety
- ✅ Buffer overflow fix prevents crashes
- ✅ Dynamic memory allocation with limits
- ✅ Bounds checking on all string operations
- ✅ File size validation before reading

### Authentication
- ✅ "login" method uses same auth as mining.auth
- ✅ No bypass of security checks
- ✅ Same credential validation

### API Security
- ✅ Read-only API (no write operations)
- ✅ No authentication required (public statistics)
- ✅ Rate limiting handled by OS/firewall
- ✅ Graceful error handling

---

## Performance Impact

### HTTP API
- Minimal overhead (separate thread)
- Non-blocking I/O
- Only active when API is queried
- No impact on mining performance

### Other Modifications
- Login method: One string comparison per message (~10ns)
- High diff minimum: One integer comparison per vardiff (~5ns)
- Extranonce: Only when requested (rare)
- User agent: String copy on worker creation (negligible)

**Overall:** No measurable performance impact on mining operations.

---

## Maintenance Notes

### Updating from Upstream
If updating from original CKPool:
1. Review this document for all modifications
2. Manually merge changes (no automatic merge possible)
3. Test thoroughly before deployment
4. Deploy to production instances sequentially

### Adding New Modifications
1. Document changes in this file
2. Test thoroughly before deployment
3. Follow established patterns for consistency

---

## License

This modified version maintains the original GPLv3 license from CKPool.

**Original Author:** Con Kolivas  
**Original Source:** https://bitbucket.org/ckolivas/ckpool/  
**Modified By:** AtlasPool Development Team  
**Modifications:** © 2025 AtlasPool

---

**Last Updated:** January 13, 2026  
**Document Version:** 1.0  
**Code Version:** Based on CKPool master branch (October 2025)
