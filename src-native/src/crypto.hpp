// crypto.hpp - AES-256-GCM encryption utilities using OpenSSL
// Used to encrypt the file index database at rest

#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace crypto {

// Derive a 256-bit key from a password using PBKDF2
// Uses salt + 100,000 iterations for security
std::vector<uint8_t> derive_key(const std::string& password, 
                                 const std::vector<uint8_t>& salt);

// Generate a cryptographically secure random salt (16 bytes)
std::vector<uint8_t> generate_salt();

// Generate a cryptographically secure random IV (12 bytes for GCM)
std::vector<uint8_t> generate_iv();

// Encrypt data using AES-256-GCM
// Returns: IV (12 bytes) + Ciphertext + Tag (16 bytes)
std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext,
                              const std::vector<uint8_t>& key);

// Decrypt data that was encrypted with encrypt()
// Input must include IV (12 bytes) + Ciphertext + Tag (16 bytes)
std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext,
                              const std::vector<uint8_t>& key);

// Encrypt a file in-place
// Creates: original_path + encrypted content
// Deletes: original file after successful encryption
bool encrypt_file(const std::string& path, const std::vector<uint8_t>& key);

// Decrypt a file in-place
// Replaces encrypted content with plaintext
bool decrypt_file(const std::string& path, const std::vector<uint8_t>& key);

// Check if a file appears to be encrypted (has our magic header)
bool is_encrypted_file(const std::string& path);

// Hash a device ID to create a machine-specific key component
std::vector<uint8_t> hash_device_id(const std::string& device_id);

// Create a keyfile path for storing the encrypted database key
std::string get_keyfile_path();

// Store a key encrypted with machine-specific data
bool store_encrypted_key(const std::vector<uint8_t>& key);

// Retrieve and decrypt the stored key
std::vector<uint8_t> retrieve_encrypted_key();

// Magic header to identify our encrypted files
constexpr uint8_t MAGIC[8] = {'P', 'D', 'C', 'R', 'Y', 'P', 'T', '1'};
constexpr size_t IV_SIZE = 12;
constexpr size_t TAG_SIZE = 16;
constexpr size_t SALT_SIZE = 16;
constexpr size_t KEY_SIZE = 32;  // 256 bits

} // namespace crypto

#endif // CRYPTO_HPP
