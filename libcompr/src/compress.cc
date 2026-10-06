#include <libcompr.h>
#include <cstring>
#include <zlib.h>
#include <zstd_errors.h>
#include <zstd.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#if defined _M_IX86
#elif defined _M_X64
#else
#endif

#if defined(_MSC_VER)
#pragma warning(disable : 4267)
#endif

#define CHUNK           16384
#define windowBits      15
#define GZIP_ENCODING   16
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6
/* Allowed flush values; see deflate() and inflate() below for details */

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO         (-1)
#define Z_STREAM_ERROR  (-2)
#define Z_DATA_ERROR    (-3)
#define Z_MEM_ERROR     (-4)
#define Z_BUF_ERROR     (-5)
#define Z_VERSION_ERROR (-6)
/* Return codes for the compression/decompression functions. Negative values
	* are errors, positive values are used for special but normal events.
	*/
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)
	/* compression levels */
#define Z_FILTERED         1
#define Z_HUFFMAN_ONLY     2
#define Z_RLE              3
#define Z_FIXED            4
#define Z_DEFAULT_STRATEGY 0
/* compression strategy; see deflateInit2() below for details */
#define Z_BINARY  0
#define Z_TEXT    1
#define Z_ASCII   Z_TEXT /* for compatibility with 1.2.2 and earlier */
#define Z_UNKNOWN 2
/* Possible values of the data_type field for deflate() */
#define Z_DEFLATED 8
/* The deflate compression method (the only one supported in this version) */
#define Z_NULL 0 /* for initializing zalloc, zfree, opaque */
#ifndef MAX_WBITS
#define MAX_WBITS 15 /* 32K LZ77 window */
#endif

bool Compress::IsZipCompress(const std::string& buffer) {
	if (buffer.empty())
		return false;
	if (buffer.size() < sizeof(std::int64_t))
		return false;
	std::int64_t head = 0;
	::memcpy(&head, buffer.data(), sizeof(std::int64_t));
	return (head << (8 * 4)) >> (8 * 4) == 0x04034b50 /*0x0000001404034b50*/;
}

bool Compress::zipCompress(const std::string& src, std::string& dest) {
	bool result = false;
	dest.clear();
	Bytef* pCompress = nullptr;
	do {
		uLongf nCompress = compressBound(static_cast<uLongf>(src.size()));
		pCompress = new Bytef[nCompress];
		if (Z_OK != compress(pCompress, &nCompress, (Bytef*)src.data(),
			static_cast<uLong>(src.size())))
			break;
		dest.append((char*)pCompress, nCompress);
		result = true;
	} while (0);
	if (pCompress) {
		delete[] pCompress;
		pCompress = nullptr;
	}
	return result;
}

bool Compress::zipUnCompress(const std::string& src, const size_t& nraw,
	std::string& dest) {
	bool result = false;
	dest.clear();
	Bytef* pUnCompress = nullptr;
	do {
		pUnCompress = new Bytef[nraw];
		if (Z_OK != uncompress(pUnCompress, (uLongf*)&nraw, (Bytef*)src.data(),
			static_cast<uLong>(src.size())))
			break;
		dest.append((char*)pUnCompress, nraw);
		result = true;
	} while (0);
	if (pUnCompress) {
		delete[] pUnCompress;
		pUnCompress = nullptr;
	}
	return result;
}

bool Compress::gzipCompress(const std::string& data, std::string& compressedData,
	int level /*= -1*/) {
	bool result = false;
	unsigned char* out = nullptr;
	do {
		bool success = true;
		out = new unsigned char[CHUNK];
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		if (deflateInit2_(&strm, level, Z_DEFLATED, windowBits | GZIP_ENCODING, 8,
			0, ZLIB_VERSION,
			(int)sizeof(z_stream) /*Z_DEFAULT_STRATEGY*/) != Z_OK)
			break;
		strm.next_in = (unsigned char*)data.c_str();
		strm.avail_in = static_cast<unsigned int>(data.size());
		do {
			int have;
			strm.avail_out = CHUNK;
			strm.next_out = out;
			if (deflate(&strm, Z_FINISH) == Z_STREAM_ERROR) {
				success = false;
				break;
			}
			have = CHUNK - strm.avail_out;
			compressedData.append((char*)out, have);
		} while (strm.avail_out == 0);
		if (!success)
			break;
		if (deflateEnd(&strm) != Z_OK)
			break;
		result = true;
	} while (0);
	if (out) {
		delete[] out;
		out = nullptr;
	}
	return result;
}
bool Compress::gzipUnCompress(const std::string& compressedData,
	std::string& data) {
	int result = false;
	unsigned char* out = nullptr;
	do {
		unsigned have;
		z_stream strm;
		out = new unsigned char[CHUNK];
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = 0;
		strm.next_in = Z_NULL;
		if (inflateInit2_(&strm, 16 + MAX_WBITS, ZLIB_VERSION,
			(int)sizeof(z_stream)) != Z_OK)
			break;
		strm.avail_in = static_cast<unsigned int>(compressedData.size());
		strm.next_in = (unsigned char*)compressedData.c_str();
		bool success = true;
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			int ret = inflate(&strm, Z_NO_FLUSH);
			if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
				inflateEnd(&strm);
				success = false;
				break;
			}
			have = CHUNK - strm.avail_out;
			data.append((char*)out, have);
		} while (strm.avail_out == 0);
		if (!success)
			break;
		if (inflateEnd(&strm) != Z_OK)
			break;
		result = true;
	} while (0);
	if (out) {
		delete[] out;
		out = nullptr;
	}
	return result;
}

namespace {
// Check ZSTD frame magic: bytes 0..3 == 0x28 B5 2F FD
	static bool zstdIsCompressed(const void* data, size_t size) {
		if (!data || size < 4) return false;
		const uint8_t* b = static_cast<const uint8_t*>(data);
		return b[0] == 0x28 && b[1] == 0xB5 && b[2] == 0x2F && b[3] == 0xFD;
	}

	static bool zstdCompressMem(const void* src, size_t srcSize, std::vector<uint8_t>& out, int level = 3) {
		if (!src) return false;
		size_t bound = ZSTD_compressBound(srcSize);
		out.resize(bound);
		size_t cSize = ZSTD_compress(out.data(), bound, src, srcSize, level);
		if (ZSTD_isError(cSize)) { out.clear(); return false; }
		out.resize(cSize);
		return true;
	}

	static bool zstdDecompressMem(const void* src, size_t srcSize, std::vector<uint8_t>& out) {
		if (!src) return false;
		unsigned long long fsz = ZSTD_getFrameContentSize(src, srcSize);
		if (fsz != ZSTD_CONTENTSIZE_ERROR && fsz != ZSTD_CONTENTSIZE_UNKNOWN) {
			// Known size — fast path
			out.resize(static_cast<size_t>(fsz));
			size_t r = ZSTD_decompress(out.data(), out.size(), src, srcSize);
			if (ZSTD_isError(r)) { out.clear(); return false; }
			// ZSTD_decompress may return actual decompressed size; resize accordingly
			out.resize(r);
			return true;
		}

		// Unknown size — streaming decompression from memory
		ZSTD_DStream* dstream = ZSTD_createDStream();
		if (!dstream) return false;
		if (ZSTD_isError(ZSTD_initDStream(dstream))) { ZSTD_freeDStream(dstream); return false; }

		const size_t inBufSize = ZSTD_DStreamInSize();
		const size_t outBufSize = ZSTD_DStreamOutSize();
		const uint8_t* inputPtr = static_cast<const uint8_t*>(src);
		size_t inputRemaining = srcSize;

		std::vector<uint8_t> tmpOut;
		tmpOut.reserve(outBufSize);

		ZSTD_inBuffer in{ nullptr, 0, 0 };
		// We'll feed from inputPtr directly
		while (true) {
			if (in.pos == in.size && inputRemaining > 0) {
				size_t feed = (inputRemaining > inBufSize) ? inBufSize : inputRemaining;
				in.src = inputPtr + (srcSize - inputRemaining);
				in.size = feed;
				in.pos = 0;
				inputRemaining -= feed;
			}

			ZSTD_outBuffer outbuf{ nullptr, 0, 0 };
			std::vector<uint8_t> outChunk(outBufSize);
			outbuf.dst = outChunk.data();
			outbuf.size = outBufSize;
			outbuf.pos = 0;

			size_t ret = ZSTD_decompressStream(dstream, &outbuf, &in);
			if (ZSTD_isError(ret)) { ZSTD_freeDStream(dstream); return false; }
			if (outbuf.pos) {
				tmpOut.insert(tmpOut.end(), outChunk.data(), outChunk.data() + outbuf.pos);
			}

			// finished when ret == 0 and no more input to provide and in.pos==in.size
			if (ret == 0 && inputRemaining == 0 && in.pos == in.size) break;
			// continue loop to consume remaining input/output
			if (inputRemaining == 0 && in.pos == in.size && ret != 0) {
				// continue to flush internal buffers
				// next iteration will call decompressStream with empty input (in.size==0)
				in.src = nullptr;
				in.size = 0;
				in.pos = 0;
			}
		}

		ZSTD_freeDStream(dstream);
		out.swap(tmpOut);
		return true;
	}
}//namespace 
bool Compress::IsZstd(const std::string& buffer) {
	if (buffer.empty())
		return false;
	return zstdIsCompressed(buffer.data(), buffer.size());
}
bool Compress::zstCompress(const std::string& src, std::string& dst) {
	bool result = false;
	dst.clear();
	do {
		if (src.empty())
			break;
		std::vector<std::uint8_t> out;
		if (!zstdCompressMem(src.data(), src.size(), out))
			break;
		dst.assign(out.begin(), out.end());
		result = true;
	} while (0);
	return result;
}
bool Compress::zstUnCompress(const std::string& src, std::string& dst) {
	bool result = false;
	dst.clear();
	do {
		if (src.empty())
			break;
		std::vector<std::uint8_t> out;
		if (!zstdDecompressMem(src.data(), src.size(), out))
			break;
		dst.assign(out.begin(), out.end());
		result = true;
	} while (0);
	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool Compress::IsBrotli(const std::string& buffer) {
	bool result = false;
	BrotliDecoderState* s = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
	do {
		if (!s)
			break;
		if (buffer.empty())
			break;
		const uint8_t* next_in = reinterpret_cast<const uint8_t*>(buffer.data());
		size_t avail_in = buffer.size();
		uint8_t outbuf[1];
		uint8_t* next_out = outbuf;
		size_t avail_out = 1;
		if (BrotliDecoderResult::BROTLI_DECODER_RESULT_ERROR == BrotliDecoderDecompressStream(
			s, &avail_in, &next_in, &avail_out, &next_out, nullptr))
			break;
		result = true;
	} while (0);
	if (s)
		BrotliDecoderDestroyInstance(s);
	return result;
}
bool Compress::brotliCompress(const std::string& src, std::string& dst) {
	dst.clear();
	if (src.empty()) return true;
	// 调整 quality 以提高速度：1..11。quality=4 是常用的速度/压缩率折中。
	const int quality = 4;
	const int lgwin = 20; // 16..22 常用，减小可提速并减小内存
	size_t max_out = BrotliEncoderMaxCompressedSize(src.size());
	std::vector<uint8_t> compressed(max_out);
	size_t compressed_size = max_out;

	if (!BrotliEncoderCompress(quality, lgwin, BROTLI_DEFAULT_MODE,
		src.size(),
		reinterpret_cast<const uint8_t*>(src.data()),
		&compressed_size,
		compressed.data())) {
		return false;
	}
	dst.assign(reinterpret_cast<const char*>(compressed.data()), compressed_size);
	return true;
}
bool Compress::brotliUnCompress(const std::string &src, std::string &dst) {
	bool result = false;
	dst.clear();
	if (src.empty()) 
		return true;
	BrotliDecoderState* s = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
	do {
		if (!s)
			break;
		std::vector<uint8_t> out;
		const uint8_t* next_in = reinterpret_cast<const uint8_t*>(src.data());
		size_t avail_in = src.size();
		std::vector<uint8_t> buf(CHUNK);

		bool ok = false;
		while (true) {
			uint8_t* next_out = buf.data();
			size_t avail_out = CHUNK;
			BrotliDecoderResult r =
				BrotliDecoderDecompressStream(
					s,
					&avail_in, &next_in,
					&avail_out, &next_out,
					nullptr);
			size_t produced = CHUNK - avail_out;
			if (produced)
				out.insert(out.end(), buf.data(), buf.data() + produced);
			if (r == BROTLI_DECODER_RESULT_SUCCESS) {
				ok = true;
				break;
			}
			if (r == BROTLI_DECODER_RESULT_ERROR) {
				ok = false;
				break;
			}
			if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && avail_in == 0) {
				ok = false;
				break;
			}
		}

		if (!ok)
			break;
		dst.assign(reinterpret_cast<const char*>(out.data()), out.size());
		result = true;
	} while (0);
	if (s) {
		BrotliDecoderDestroyInstance(s);
	}
	return result;
}



#if 0
bool BrotliDecompressStream(const uint8_t* in, size_t in_size, std::vector<uint8_t>& out) {
	BrotliDecoderState* s = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
	if (!s) return false;
	const uint8_t* next_in = in;
	size_t avail_in = in_size;
	const size_t CHUNK = 64 * 1024;
	uint8_t buf[CHUNK];

	bool ok = false;
	while (true) {
		uint8_t* next_out = buf;
		size_t avail_out = CHUNK;
		BrotliDecoderResult r = BrotliDecoderDecompressStream(s,
			&avail_in, &next_in,
			&avail_out, &next_out,
			nullptr);
		size_t produced = CHUNK - avail_out;
		if (produced) out.insert(out.end(), buf, buf + produced);

		if (r == BROTLI_DECODER_RESULT_SUCCESS) { ok = true; break; }
		if (r == BROTLI_DECODER_RESULT_ERROR) { ok = false; break; }
		// 如果还需要输入但已无数据，则失败
		if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && avail_in == 0) { ok = false; break; }
		// 否则继续循环（会消耗输入或产生更多输出）
	}

	BrotliDecoderDestroyInstance(s);
	return ok;
}
#endif
