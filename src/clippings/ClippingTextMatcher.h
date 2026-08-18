#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ClippingTextMatcher {

enum class TokenFragmentMatch : uint8_t {
  MISMATCH,
  CONTINUES_TOKEN,
  COMPLETES_TOKEN,
};

// Compares one rendered word fragment with a saved clipping token. Layout can
// split one logical token into adjacent display fragments (for example, a word
// followed by an ellipsis when focus reading is enabled). An inserted hyphen is
// omitted from the saved token before matching its fragment.
inline TokenFragmentMatch matchTokenFragment(const char* word, const bool endsWithInsertedHyphen, const char* token,
                                             const size_t tokenLen, const size_t tokenOffset) {
  if (!word || !token || tokenLen == 0 || tokenOffset >= tokenLen) {
    return TokenFragmentMatch::MISMATCH;
  }

  size_t wordLen = std::strlen(word);
  if (endsWithInsertedHyphen) {
    if (wordLen == 0 || word[wordLen - 1] != '-') {
      return TokenFragmentMatch::MISMATCH;
    }
    wordLen--;
  }

  const size_t remainingLen = tokenLen - tokenOffset;
  if (wordLen == 0 || wordLen > remainingLen || std::strncmp(word, token + tokenOffset, wordLen) != 0) {
    return TokenFragmentMatch::MISMATCH;
  }

  if (wordLen == remainingLen) {
    return endsWithInsertedHyphen ? TokenFragmentMatch::MISMATCH : TokenFragmentMatch::COMPLETES_TOKEN;
  }
  return TokenFragmentMatch::CONTINUES_TOKEN;
}

}  // namespace ClippingTextMatcher
