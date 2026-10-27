/* Audited buffer functions from Arduino-wolfSSL / wolfSSL 5.7.2,
 * src/src/internal.c. Copyright (C) 2006-2023 wolfSSL Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Kept verbatim (before CrossInk patches) for the isolated allocator harness.
 * No cryptography is mocked by firmware; only this native test uses stubs.
 */

/* This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

// clang-format off
void ShrinkInputBuffer(WOLFSSL* ssl, int forcedFree)
{
    int usedLength = ssl->buffers.inputBuffer.length -
                     ssl->buffers.inputBuffer.idx;
    if (!forcedFree && (usedLength > STATIC_BUFFER_LEN ||
            ssl->buffers.clearOutputBuffer.length > 0))
        return;

    WOLFSSL_MSG("Shrinking input buffer");

    if (!forcedFree && usedLength > 0) {
        XMEMCPY(ssl->buffers.inputBuffer.staticBuffer,
               ssl->buffers.inputBuffer.buffer + ssl->buffers.inputBuffer.idx,
               usedLength);
    }

    ForceZero(ssl->buffers.inputBuffer.buffer,
        ssl->buffers.inputBuffer.length);
    XFREE(ssl->buffers.inputBuffer.buffer - ssl->buffers.inputBuffer.offset,
          ssl->heap, DYNAMIC_TYPE_IN_BUFFER);
    ssl->buffers.inputBuffer.buffer = ssl->buffers.inputBuffer.staticBuffer;
    ssl->buffers.inputBuffer.bufferSize  = STATIC_BUFFER_LEN;
    ssl->buffers.inputBuffer.dynamicFlag = 0;
    ssl->buffers.inputBuffer.offset      = 0;
    ssl->buffers.inputBuffer.idx = 0;
    ssl->buffers.inputBuffer.length = (word32)usedLength;
}

int GrowInputBuffer(WOLFSSL* ssl, int size, int usedLength)
{
    byte* tmp;
#if defined(WOLFSSL_DTLS) || WOLFSSL_GENERAL_ALIGNMENT > 0
    byte  align = ssl->options.dtls ? WOLFSSL_GENERAL_ALIGNMENT : 0;
    byte  hdrSz = DTLS_RECORD_HEADER_SZ;
#else
    const byte align = WOLFSSL_GENERAL_ALIGNMENT;
#endif

#if defined(WOLFSSL_DTLS) || WOLFSSL_GENERAL_ALIGNMENT > 0
    /* the encrypted data will be offset from the front of the buffer by
       the dtls record header, if the user wants encrypted alignment they need
       to define their alignment requirement. in tls we read record header
       to get size of record and put actual data back at front, so don't need */

    if (align) {
       while (align < hdrSz)
           align *= 2;
    }
#endif

    if (usedLength < 0 || size < 0) {
        WOLFSSL_MSG("GrowInputBuffer() called with negative number");
        return BAD_FUNC_ARG;
    }

    tmp = (byte*)XMALLOC(size + usedLength + align,
                             ssl->heap, DYNAMIC_TYPE_IN_BUFFER);
    WOLFSSL_MSG("growing input buffer");

    if (tmp == NULL)
        return MEMORY_E;

#if defined(WOLFSSL_DTLS) || WOLFSSL_GENERAL_ALIGNMENT > 0
    if (align)
        tmp += align - hdrSz;
#endif

#ifdef WOLFSSL_STATIC_MEMORY
    /* can be from IO memory pool which does not need copy if same buffer */
    if (usedLength && tmp == ssl->buffers.inputBuffer.buffer) {
        ssl->buffers.inputBuffer.bufferSize = size + usedLength;
        ssl->buffers.inputBuffer.idx    = 0;
        ssl->buffers.inputBuffer.length = usedLength;
        return 0;
    }
#endif

    if (usedLength)
        XMEMCPY(tmp, ssl->buffers.inputBuffer.buffer +
                    ssl->buffers.inputBuffer.idx, usedLength);

    if (ssl->buffers.inputBuffer.dynamicFlag) {
        if (IsEncryptionOn(ssl, 1)) {
            ForceZero(ssl->buffers.inputBuffer.buffer,
                ssl->buffers.inputBuffer.length);
        }
        XFREE(ssl->buffers.inputBuffer.buffer - ssl->buffers.inputBuffer.offset,
              ssl->heap, DYNAMIC_TYPE_IN_BUFFER);
    }

    ssl->buffers.inputBuffer.dynamicFlag = 1;
#if defined(WOLFSSL_DTLS) || WOLFSSL_GENERAL_ALIGNMENT > 0
    if (align)
        ssl->buffers.inputBuffer.offset = align - hdrSz;
    else
#endif
        ssl->buffers.inputBuffer.offset = 0;

    ssl->buffers.inputBuffer.buffer = tmp;
    ssl->buffers.inputBuffer.bufferSize = size + usedLength;
    ssl->buffers.inputBuffer.idx    = 0;
    ssl->buffers.inputBuffer.length = (word32)usedLength;

    return 0;
}


static int GetInputData(WOLFSSL *ssl, word32 size)
{
    int inSz;
    int maxLength;
    int usedLength;
    int dtlsExtra = 0;


    /* check max input length */
    usedLength = ssl->buffers.inputBuffer.length - ssl->buffers.inputBuffer.idx;
    maxLength  = ssl->buffers.inputBuffer.bufferSize - usedLength;
    inSz       = (int)(size - usedLength);      /* from last partial read */

#ifdef WOLFSSL_DTLS
    if (ssl->options.dtls && IsDtlsNotSctpMode(ssl)) {
        /* Add DTLS_MTU_ADDITIONAL_READ_BUFFER bytes so that we can operate with
         * slight difference in set MTU size on each peer */
#ifdef WOLFSSL_DTLS_MTU
        inSz = (word32)ssl->dtlsMtuSz + DTLS_MTU_ADDITIONAL_READ_BUFFER;
#else
        inSz = MAX_MTU + DTLS_MTU_ADDITIONAL_READ_BUFFER;
#endif
        if (size < (word32)inSz)
            dtlsExtra = (int)(inSz - size);
    }
#endif

    /* check that no lengths or size values are negative */
    if (usedLength < 0 || maxLength < 0 || inSz <= 0) {
        return BUFFER_ERROR;
    }

    if (inSz > maxLength) {
        if (GrowInputBuffer(ssl, size + dtlsExtra, usedLength) < 0)
            return MEMORY_E;
    }

    /* Put buffer data at start if not there */
    if (usedLength > 0 && ssl->buffers.inputBuffer.idx != 0)
        XMEMMOVE(ssl->buffers.inputBuffer.buffer,
                ssl->buffers.inputBuffer.buffer + ssl->buffers.inputBuffer.idx,
                usedLength);

    /* remove processed data */
    ssl->buffers.inputBuffer.idx    = 0;
    ssl->buffers.inputBuffer.length = (word32)usedLength;

    /* read data from network */
    do {
        int in = wolfSSLReceive(ssl,
                     ssl->buffers.inputBuffer.buffer +
                     ssl->buffers.inputBuffer.length,
                     (word32)inSz);
        if (in == WANT_READ)
            return WANT_READ;

        if (in < 0) {
            WOLFSSL_ERROR_VERBOSE(SOCKET_ERROR_E);
            return SOCKET_ERROR_E;
        }

        if (in > inSz) {
            WOLFSSL_ERROR_VERBOSE(RECV_OVERFLOW_E);
            return RECV_OVERFLOW_E;
        }

        ssl->buffers.inputBuffer.length += in;
        inSz -= in;

    } while (ssl->buffers.inputBuffer.length < size);

#ifdef WOLFSSL_DEBUG_TLS
    if (ssl->buffers.inputBuffer.idx == 0) {
        WOLFSSL_MSG("Data received");
        WOLFSSL_BUFFER(ssl->buffers.inputBuffer.buffer,
                       ssl->buffers.inputBuffer.length);
    }
#endif

    return 0;
}
// clang-format on
