/*
 * HTTP API Server Implementation for CKPool
 * Provides read-only HTTP endpoints for pool statistics
 * PATCHED VERSION - Fixed buffer overflow vulnerabilities
 * 
 * Modified for AtlasPool - See README.md for details
 */

#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

#include "api_server.h"

/* Global state */
static struct MHD_Daemon *http_daemon = NULL;
static char log_dir_path[512] = "/data/ckpool/log";
static pthread_mutex_t api_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Maximum response size - 10MB limit to prevent memory exhaustion */
#define MAX_RESPONSE_SIZE (10 * 1024 * 1024)
#define INITIAL_BUFFER_SIZE (128 * 1024)
#define MAX_USER_FILE_SIZE (64 * 1024)

/* Set log directory path */
void api_server_set_log_dir(const char *log_dir) {
    if (log_dir) {
        snprintf(log_dir_path, sizeof(log_dir_path), "%s", log_dir);
    }
}

/* Helper: Safe string append with dynamic reallocation */
static int safe_append(char **buffer, size_t *buffer_size, size_t *used, const char *str, size_t str_len) {
    /* Check if we need more space */
    while (*used + str_len + 1 > *buffer_size) {
        /* Check if we've hit the maximum response size */
        if (*buffer_size >= MAX_RESPONSE_SIZE) {
            fprintf(stderr, "Response size limit exceeded (%zu bytes)\n", *buffer_size);
            return -1;
        }
        
        /* Double the buffer size */
        size_t new_size = *buffer_size * 2;
        if (new_size > MAX_RESPONSE_SIZE) {
            new_size = MAX_RESPONSE_SIZE;
        }
        
        char *new_buffer = realloc(*buffer, new_size);
        if (!new_buffer) {
            fprintf(stderr, "Failed to reallocate buffer to %zu bytes\n", new_size);
            return -1;
        }
        
        *buffer = new_buffer;
        *buffer_size = new_size;
    }
    
    /* Safe to append now */
    memcpy(*buffer + *used, str, str_len);
    *used += str_len;
    (*buffer)[*used] = '\0';
    
    return 0;
}

/* Helper: Read pool status file */
static char* read_pool_status(void) {
    char status_path[600];
    FILE *fp;
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    char *result;
    size_t buffer_size = 4096;
    size_t used = 0;
    
    snprintf(status_path, sizeof(status_path), "%s/pool/pool.status", log_dir_path);
    
    fp = fopen(status_path, "r");
    if (!fp) {
        return strdup("{\"error\":\"Cannot open pool status file\"}");
    }
    
    result = malloc(buffer_size);
    if (!result) {
        fclose(fp);
        return strdup("{\"error\":\"Memory allocation failed\"}");
    }
    
    result[0] = '\0';
    
    if (safe_append(&result, &buffer_size, &used, "[", 1) < 0) {
        free(result);
        fclose(fp);
        return strdup("{\"error\":\"Buffer overflow\"}");
    }
    
    int line_num = 0;
    while ((read = getline(&line, &len, fp)) != -1 && line_num < 3) {
        if (line_num > 0) {
            if (safe_append(&result, &buffer_size, &used, ",", 1) < 0) {
                free(result);
                free(line);
                fclose(fp);
                return strdup("{\"error\":\"Buffer overflow\"}");
            }
        }
        
        /* Remove trailing newline */
        if (read > 0 && line[read - 1] == '\n') {
            read--;
        }
        
        if (safe_append(&result, &buffer_size, &used, line, read) < 0) {
            free(result);
            free(line);
            fclose(fp);
            return strdup("{\"error\":\"Buffer overflow\"}");
        }
        
        line_num++;
    }
    
    if (safe_append(&result, &buffer_size, &used, "]", 1) < 0) {
        free(result);
        free(line);
        fclose(fp);
        return strdup("{\"error\":\"Buffer overflow\"}");
    }
    
    free(line);
    fclose(fp);
    
    return result;
}

/* Helper: Read specific user statistics */

/* Find user file with case-insensitive matching */
static char* find_user_file_case_insensitive(const char *address) {
    DIR *dir;
    struct dirent *entry;
    char users_dir[700];
    char *result = NULL;
    
    snprintf(users_dir, sizeof(users_dir), "%s/users", log_dir_path);
    
    dir = opendir(users_dir);
    if (!dir) {
        return NULL;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        /* Case-insensitive comparison */
        if (strcasecmp(entry->d_name, address) == 0) {
            result = malloc(strlen(entry->d_name) + 1);
            if (result) {
                strcpy(result, entry->d_name);
            }
            break;
        }
    }
    
    closedir(dir);
    return result;
}

static char* read_single_user_stats(const char *address) {
    char user_file[700];
    FILE *fp;
    char *result;
    struct stat st;
    size_t file_size;
    char *actual_filename;
    
    /* Find the actual filename with case-insensitive matching */
    actual_filename = find_user_file_case_insensitive(address);
    if (!actual_filename) {
        result = malloc(256);
        snprintf(result, 256, "{\"error\":\"User not found\",\"address\":\"%s\"}", address);
        return result;
    }
    
    snprintf(user_file, sizeof(user_file), "%s/users/%s", log_dir_path, actual_filename);
    free(actual_filename);
    
    /* Check file size first */
    if (stat(user_file, &st) != 0) {
        result = malloc(256);
        snprintf(result, 256, "{\"error\":\"User not found\",\"address\":\"%s\"}", address);
        return result;
    }
    
    file_size = st.st_size;
    
    /* Enforce maximum user file size */
    if (file_size > MAX_USER_FILE_SIZE) {
        result = malloc(256);
        snprintf(result, 256, "{\"error\":\"User file too large (%zu bytes)\",\"address\":\"%s\"}",
                file_size, address);
        fprintf(stderr, "User file %s is too large: %zu bytes (max %d)\n", 
                address, file_size, MAX_USER_FILE_SIZE);
        return result;
    }
    
    fp = fopen(user_file, "r");
    if (!fp) {
        result = malloc(256);
        snprintf(result, 256, "{\"error\":\"Cannot open user file\",\"address\":\"%s\"}", address);
        return result;
    }
    
    /* Allocate buffer with extra space for null terminator */
    result = malloc(file_size + 1);
    if (!result) {
        fclose(fp);
        fclose(fp);
        return strdup("{\"error\":\"Memory allocation failed\"}");
    }
    
    size_t bytes_read = fread(result, 1, file_size, fp);
    result[bytes_read] = '\0';
    fclose(fp);
    
    return result;
}

/* Helper: Read all user statistics */
static char* read_user_stats(void) {
    char users_dir[600];
    DIR *dir;
    struct dirent *entry;
    char *result;
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t used = 0;
    int first = 1;
    int user_count = 0;
    
    snprintf(users_dir, sizeof(users_dir), "%s/users", log_dir_path);
    
    dir = opendir(users_dir);
    if (!dir) {
        return strdup("{\"error\":\"Cannot open users directory\"}");
    }
    
    result = malloc(buffer_size);
    if (!result) {
        closedir(dir);
        return strdup("{\"error\":\"Memory allocation failed\"}");
    }
    
    result[0] = '\0';
    
    if (safe_append(&result, &buffer_size, &used, "{", 1) < 0) {
        free(result);
        closedir(dir);
        return strdup("{\"error\":\"Buffer overflow\"}");
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        user_count++;
        
        char user_file[700];
        snprintf(user_file, sizeof(user_file), "%s/%s", users_dir, entry->d_name);
        
        /* Check file size */
        struct stat st;
        if (stat(user_file, &st) != 0) {
            fprintf(stderr, "Cannot stat user file: %s\n", user_file);
            continue;
        }
        
        /* Skip files that are too large */
        if (st.st_size > MAX_USER_FILE_SIZE) {
            fprintf(stderr, "Skipping oversized user file: %s (%zu bytes)\n", 
                    entry->d_name, (size_t)st.st_size);
            continue;
        }
        
        FILE *fp = fopen(user_file, "r");
        if (!fp) {
            fprintf(stderr, "Cannot open user file: %s\n", user_file);
            continue;
        }
        
        /* Allocate buffer for user stats */
        char *user_stats = malloc(st.st_size + 1);
        if (!user_stats) {
            fprintf(stderr, "Memory allocation failed for user: %s\n", entry->d_name);
            fclose(fp);
            continue;
        }
        
        size_t bytes_read = fread(user_stats, 1, st.st_size, fp);
        user_stats[bytes_read] = '\0';
        fclose(fp);
        
        /* Add comma separator if not first entry */
        if (!first) {
            if (safe_append(&result, &buffer_size, &used, ",", 1) < 0) {
                free(user_stats);
                free(result);
                closedir(dir);
                return strdup("{\"error\":\"Buffer overflow\"}");
            }
        }
        
        /* Add user address as key */
        if (safe_append(&result, &buffer_size, &used, "\"", 1) < 0 ||
            safe_append(&result, &buffer_size, &used, entry->d_name, strlen(entry->d_name)) < 0 ||
            safe_append(&result, &buffer_size, &used, "\":", 2) < 0) {
            free(user_stats);
            free(result);
            closedir(dir);
            return strdup("{\"error\":\"Buffer overflow\"}");
        }
        
        /* Add user stats */
        if (safe_append(&result, &buffer_size, &used, user_stats, bytes_read) < 0) {
            free(user_stats);
            free(result);
            closedir(dir);
            return strdup("{\"error\":\"Buffer overflow\"}");
        }
        
        free(user_stats);
        first = 0;
    }
    
    if (safe_append(&result, &buffer_size, &used, "}", 1) < 0) {
        free(result);
        closedir(dir);
        return strdup("{\"error\":\"Buffer overflow\"}");
    }
    
    closedir(dir);
    
    printf("Processed %d users, response size: %zu bytes\n", user_count, used);
    
    return result;
}

/* HTTP request handler */
static enum MHD_Result handle_request(void *cls,
                                     struct MHD_Connection *connection,
                                     const char *url,
                                     const char *method,
                                     const char *version,
                                     const char *upload_data,
                                     size_t *upload_data_size,
                                     void **con_cls) {
    struct MHD_Response *response;
    enum MHD_Result ret;
    char *page_content = NULL;
    int status_code = MHD_HTTP_OK;
    
    /* Only handle GET requests */
    if (strcmp(method, "GET") != 0) {
        page_content = strdup("{\"error\":\"Only GET method supported\"}");
        status_code = MHD_HTTP_METHOD_NOT_ALLOWED;
    }
    /* Handle /api/status endpoint */
    else if (strcmp(url, "/api/status") == 0 || strcmp(url, "/api/status/") == 0) {
        time_t now = time(NULL);
        page_content = malloc(256);
        snprintf(page_content, 256, 
                "{\"status\":\"ok\",\"timestamp\":%ld,\"message\":\"CKPool API Server is running\"}", 
                now);
    }
    /* Handle /api/pool endpoint */
    else if (strcmp(url, "/api/pool") == 0 || strcmp(url, "/api/pool/") == 0) {
        page_content = read_pool_status();
    }
    /* Handle /api/users endpoint - all users */
    else if (strcmp(url, "/api/users") == 0 || strcmp(url, "/api/users/") == 0) {
        page_content = read_user_stats();
    }
    /* Handle /api/users/{address} endpoint - specific user */
    else if (strncmp(url, "/api/users/", 11) == 0) {
        const char *address = url + 11;
        if (strlen(address) > 0 && strlen(address) < 100) {
            page_content = read_single_user_stats(address);
        } else {
            page_content = strdup("{\"error\":\"Invalid user address\"}");
            status_code = MHD_HTTP_BAD_REQUEST;
        }
    }
    /* Handle root / endpoint */
    else if (strcmp(url, "/") == 0) {
        page_content = strdup("{\"name\":\"CKPool API Server\","
                            "\"version\":\"1.0.1-patched\","
                            "\"endpoints\":[\"/api/status\",\"/api/pool\",\"/api/users\",\"/api/users/{address}\"]}");
    }
    /* 404 for unknown endpoints */
    else {
        page_content = malloc(256);
        snprintf(page_content, 256, "{\"error\":\"Not found\",\"path\":\"%s\"}", url);
        status_code = MHD_HTTP_NOT_FOUND;
    }
    
    /* Create response */
    response = MHD_create_response_from_buffer(strlen(page_content),
                                              (void*)page_content,
                                              MHD_RESPMEM_MUST_FREE);
    
    /* Add headers */
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    
    /* Queue response */
    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    
    return ret;
}

/* Initialize HTTP server */
int api_server_init(int port) {
    pthread_mutex_lock(&api_mutex);
    
    if (http_daemon) {
        fprintf(stderr, "API server already running\n");
        pthread_mutex_unlock(&api_mutex);
        return -1;
    }
    
    http_daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
                                  port,
                                  NULL, NULL,
                                  &handle_request, NULL,
                                  MHD_OPTION_END);
    
    if (!http_daemon) {
        fprintf(stderr, "Failed to start API server on port %d\n", port);
        pthread_mutex_unlock(&api_mutex);
        return -1;
    }
    
    printf("API server started successfully on port %d (PATCHED VERSION)\n", port);
    printf("Endpoints available:\n");
    printf("  GET http://localhost:%d/api/status - Server status\n", port);
    printf("  GET http://localhost:%d/api/pool   - Pool statistics\n", port);
    printf("  GET http://localhost:%d/api/users  - All user statistics\n", port);
    printf("  GET http://localhost:%d/api/users/{address} - Specific user statistics\n", port);
    
    pthread_mutex_unlock(&api_mutex);
    return 0;
}

/* Stop HTTP server */
void api_server_stop(void) {
    pthread_mutex_lock(&api_mutex);
    
    if (http_daemon) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
        printf("API server stopped\n");
    }
    
    pthread_mutex_unlock(&api_mutex);
}
