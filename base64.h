#include <string>
std::string base64_decode(std::string const &encoded_string);
std::string base64_encode(unsigned char const *bytes_to_encode, unsigned int in_len);
static inline bool is_base64(unsigned char c);