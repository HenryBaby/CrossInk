/* Native allocator/IO stubs for the audited wolfSSL buffer functions only. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char byte;
typedef uint32_t word32;
#define STATIC_BUFFER_LEN 5
#define RECORD_HEADER_SZ 5
#define MAX_RECORD_SIZE 16384
#define MAX_COMP_EXTRA 1024
#define MAX_MSG_EXTRA (38 + 64)
#define CAPACITY (MAX_RECORD_SIZE + MAX_COMP_EXTRA + MAX_MSG_EXTRA + RECORD_HEADER_SZ)
#define WOLFSSL_GENERAL_ALIGNMENT 0
#define MEMORY_E -125
#define BAD_FUNC_ARG -173
#define BUFFER_ERROR -132
#define WANT_READ -323
#define SOCKET_ERROR_E -308
#define RECV_OVERFLOW_E -376
#define WOLFSSL_MSG(msg) ((void)0)
#define WOLFSSL_ERROR_VERBOSE(err) ((void)0)
#define XMEMCPY memcpy
#define XMEMMOVE memmove
#define DYNAMIC_TYPE_IN_BUFFER 0

typedef struct {
  byte staticBuffer[STATIC_BUFFER_LEN];
  byte* buffer;
  word32 bufferSize, length, idx;
  int dynamicFlag, offset;
} Buffer;
typedef struct {
  struct {
    Buffer inputBuffer, clearOutputBuffer;
  } buffers;
  struct {
    int handShakeDone, dtls;
  } options;
  void* heap;
} WOLFSSL;

/* A real first-fit arena: freeing alternating pinned blocks leaves plenty of
 * total free RAM, but no free run large enough for a legal full TLS record. */
static byte arena[65536];
static unsigned short allocated[4096];
static byte occupied[4096];
static int allocations, frees, receives, receive_result;

static void* allocate(size_t size) {
  size_t units = (size + 15) / 16, run = 0;
  for (size_t i = 0; i < sizeof(occupied); ++i) {
    run = occupied[i] ? 0 : run + 1;
    if (run == units) {
      size_t start = i + 1 - units;
      memset(occupied + start, 1, units);
      allocated[start] = (unsigned short)units;
      ++allocations;
      return arena + start * 16;
    }
  }
  return NULL;
}

static void release(void* ptr) {
  size_t start = ((byte*)ptr - arena) / 16;
  assert(start < sizeof(occupied) && allocated[start]);
  memset(occupied + start, 0, allocated[start]);
  allocated[start] = 0;
  ++frees;
}

#define XMALLOC(size, heap, type) allocate(size)
#define XFREE(ptr, heap, type) release(ptr)
static void ForceZero(void* ptr, size_t size) { memset(ptr, 0, size); }
static int IsEncryptionOn(WOLFSSL* ssl, int writing) {
  (void)ssl;
  (void)writing;
  return 1;
}
static int wolfSSLReceive(WOLFSSL* ssl, byte* ptr, word32 size) {
  (void)ssl;
  ++receives;
  if (receive_result) return receive_result;
  memset(ptr, 0x5a, size);
  return (int)size;
}

/* Python writes the exact patched fixture beside this harness. */
#include "wolfssl_input_buffer.c"

static void reset(WOLFSSL* ssl) {
  memset(occupied, 0, sizeof(occupied));
  memset(allocated, 0, sizeof(allocated));
  memset(ssl, 0, sizeof(*ssl));
  allocations = frees = receives = receive_result = 0;
  ssl->buffers.inputBuffer.buffer = ssl->buffers.inputBuffer.staticBuffer;
  ssl->buffers.inputBuffer.bufferSize = STATIC_BUFFER_LEN;
}

static int read_record(WOLFSSL* ssl, int size) {
  int result = GetInputData(ssl, (word32)size);
  if (result == 0) {
    ssl->buffers.inputBuffer.idx = ssl->buffers.inputBuffer.length;
    if (ssl->buffers.inputBuffer.dynamicFlag) ShrinkInputBuffer(ssl, 0);
  }
  return result;
}

static void cleanup(WOLFSSL* ssl) {
  if (ssl->buffers.inputBuffer.dynamicFlag) ShrinkInputBuffer(ssl, 1);
  for (size_t i = 0; i < sizeof(occupied); ++i) assert(!occupied[i]);
  assert(!ssl->buffers.inputBuffer.dynamicFlag);
  assert(ssl->buffers.inputBuffer.buffer == ssl->buffers.inputBuffer.staticBuffer);
}

int main(void) {
  WOLFSSL ssl;
  void* pins[16];
  int count;

  reset(&ssl);
  /* Handshake reads retain their existing small allocation policy. */
  assert(read_record(&ssl, 2048) == 0);
  assert(ssl.buffers.inputBuffer.bufferSize == 2048);
  ssl.options.handShakeDone = 1;
  assert(read_record(&ssl, 4096) == 0);
#if EXPECT_RESERVE
  assert(ssl.buffers.inputBuffer.bufferSize == CAPACITY);
#else
  assert(ssl.buffers.inputBuffer.bufferSize == 4096);
#endif
  count = 0;
  while (count < 16 && (pins[count] = allocate(4096)) != NULL) ++count;
  for (int i = 0; i < count; i += 2) release(pins[i]);
  int before = allocations;
#if EXPECT_RESERVE
  for (int size = 8192; size <= MAX_RECORD_SIZE; size += 4096) assert(read_record(&ssl, size) == 0);
  /* Include maximum TLS1.3 padding and the audited record-layer bound. */
  assert(read_record(&ssl, MAX_RECORD_SIZE + 256) == 0);
  assert(read_record(&ssl, CAPACITY) == 0);
#else
  assert(read_record(&ssl, MAX_RECORD_SIZE) == MEMORY_E);
#endif
  assert(allocations == before);
  for (int i = 1; i < count; i += 2) release(pins[i]);
  cleanup(&ssl);

  reset(&ssl);
  ssl.options.handShakeDone = 1;
  ssl.options.dtls = 1;
  assert(read_record(&ssl, 128) == 0);
  assert(ssl.buffers.inputBuffer.bufferSize == 128);
  cleanup(&ssl);

  reset(&ssl);
  ssl.options.handShakeDone = 1;
  /* Preserve unread data and indexes on reserve OOM; never call IO. */
  memcpy(ssl.buffers.inputBuffer.staticBuffer, "abcde", 5);
  ssl.buffers.inputBuffer.length = 5;
  ssl.buffers.inputBuffer.idx = 2;
  void* pin = allocate(sizeof(arena) - 8192);
  assert(pin);
#if EXPECT_RESERVE
  assert(GetInputData(&ssl, 5) == MEMORY_E);
  assert(receives == 0);
  assert(ssl.buffers.inputBuffer.idx == 2);
  assert(ssl.buffers.inputBuffer.length == 5);
  assert(memcmp(ssl.buffers.inputBuffer.staticBuffer, "abcde", 5) == 0);
#endif
  release(pin);
  cleanup(&ssl);

  reset(&ssl);
  assert(read_record(&ssl, 2048) == 0);
  byte* handshake_buffer = ssl.buffers.inputBuffer.buffer;
  ssl.options.handShakeDone = 1;
  pin = allocate(sizeof(arena) - 2048 - 8192);
  assert(pin);
#if EXPECT_RESERVE
  before = receives;
  assert(GetInputData(&ssl, 5) == MEMORY_E);
  assert(receives == before);
  assert(ssl.buffers.inputBuffer.buffer == handshake_buffer);
  assert(ssl.buffers.inputBuffer.dynamicFlag);
  assert(ssl.buffers.inputBuffer.bufferSize == 2048);
#else
  (void)handshake_buffer;
#endif
  release(pin);
  cleanup(&ssl);
  assert(allocations == frees);

  reset(&ssl);
  ssl.options.handShakeDone = 1;
  memcpy(ssl.buffers.inputBuffer.staticBuffer, "abcde", 5);
  ssl.buffers.inputBuffer.length = 5;
  ssl.buffers.inputBuffer.idx = 2;
  assert(GetInputData(&ssl, 8) == 0);
  assert(memcmp(ssl.buffers.inputBuffer.buffer, "cde", 3) == 0);
  assert(ssl.buffers.inputBuffer.length == 8);
  assert(ssl.buffers.inputBuffer.idx == 0);
#if EXPECT_RESERVE
  assert(ssl.buffers.inputBuffer.bufferSize == CAPACITY);
#endif
  cleanup(&ssl);

  /* Cancellation after WANT_READ and transport failure use forced cleanup. */
  for (int mode = 0; mode < 2; ++mode) {
    reset(&ssl);
    ssl.options.handShakeDone = 1;
    receive_result = mode ? -1 : WANT_READ;
    assert(GetInputData(&ssl, 128) == (mode ? SOCKET_ERROR_E : WANT_READ));
    cleanup(&ssl);
    assert(allocations == frees);
  }
  puts("wolfSSL allocator scenarios passed");
  return 0;
}
