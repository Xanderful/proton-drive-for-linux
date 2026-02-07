// crypto.cpp - AES-256-GCM encryption utilities implementation
// Uses OpenSSL for cryptographic operations

#include "crypto.hpp"
#include "logger.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#include <fstream>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

namespace crypto {

std::vector<uint8_t> derive_key(const std::string& password, 
                                 const std::vector<uint8_t>& salt) {
    std::vector<uint8_t> key(KEY_SIZE);
    
    // PBKDF2 with SHA-256, 100,000 iterations
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                          salt.data(), salt.size(),
                          100000, EVP_sha256(),
                          KEY_SIZE, key.data()) != 1) {
        Logger::error("[Crypto] PBKDF2 key derivation failed");
        return {};
    }
    
    return key;
}

std::vector<uint8_t> generate_salt() {
    std::vector<uint8_t> salt(SALT_SIZE);
    if (RAND_bytes(salt.data(), SALT_SIZE) != 1) {
        Logger::error("[Crypto] Failed to generate random salt");
        return {};
    }
    return salt;
}

std::vector<uint8_t> generate_iv() {
    std::vector<uint8_t> iv(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1) {
        Logger::error("[Crypto] Failed to generate random IV");
        return {};
    }
    return iv;
}

std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext,
                              const std::vector<uint8_t>& key) {
    if (key.size() != KEY_SIZE) {
        Logger::error("[Crypto] Invalid key size: " + std::to_string(key.size()));
        return {};
    }
    
    // Generate random IV
    std::vector<uint8_t> iv = generate_iv();
    if (iv.empty()) return {};
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        Logger::error("[Crypto] Failed to create cipher context");
        return {};
    }
    
    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                           key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Failed to initialize encryption");
        return {};
    }
    
    // Allocate output buffer (IV + ciphertext + tag)
    std::vector<uint8_t> ciphertext(IV_SIZE + plaintext.size() + TAG_SIZE);
    
    // Copy IV to output
    std::memcpy(ciphertext.data(), iv.data(), IV_SIZE);
    
    // Encrypt
    int len;
    if (EVP_EncryptUpdate(ctx, ciphertext.data() + IV_SIZE, &len, 
                          plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Encryption update failed");
        return {};
    }
    
    int ciphertext_len = len;
    
    // Finalize
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + IV_SIZE + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Encryption finalization failed");
        return {};
    }
    ciphertext_len += len;
    
    // Get tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, 
                            ciphertext.data() + IV_SIZE + ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Failed to get authentication tag");
        return {};
    }
    
    EVP_CIPHER_CTX_free(ctx);
    
    // Resize to actual size
    ciphertext.resize(IV_SIZE + ciphertext_len + TAG_SIZE);
    
    return ciphertext;
}

std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext,
                              const std::vector<uint8_t>& key) {
    if (key.size() != KEY_SIZE) {
        Logger::error("[Crypto] Invalid key size for decryption");
        return {};
    }
    
    if (ciphertext.size() < IV_SIZE + TAG_SIZE) {
        Logger::error("[Crypto] Ciphertext too short");
        return {};
    }
    
    // Extract IV
    std::vector<uint8_t> iv(ciphertext.begin(), ciphertext.begin() + IV_SIZE);
    
    // Extract tag (last TAG_SIZE bytes)
    std::vector<uint8_t> tag(ciphertext.end() - TAG_SIZE, ciphertext.end());
    
    // Extract actual ciphertext
    size_t ct_len = ciphertext.size() - IV_SIZE - TAG_SIZE;
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        Logger::error("[Crypto] Failed to create cipher context for decryption");
        return {};
    }
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                           key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Failed to initialize decryption");
        return {};
    }
    
    // Decrypt
    std::vector<uint8_t> plaintext(ct_len);
    int len;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, 
                          ciphertext.data() + IV_SIZE, ct_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Decryption update failed");
        return {};
    }
    
    int plaintext_len = len;
    
    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, 
                            const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        Logger::error("[Crypto] Failed to set authentication tag");
        return {};
    }
    
    // Finalize and verify tag
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    
    if (ret <= 0) {
        Logger::error("[Crypto] Authentication failed - data may be corrupted or tampered");
        return {};
    }
    
    plaintext_len += len;
    plaintext.resize(plaintext_len);
    
    return plaintext;
}

bool is_encrypted_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    
    uint8_t header[sizeof(MAGIC)];
    file.read(reinterpret_cast<char*>(header), sizeof(MAGIC));
    
    return file.gcount() == sizeof(MAGIC) && 
           std::memcmp(header, MAGIC, sizeof(MAGIC)) == 0;
}

bool encrypt_file(const std::string& path, const std::vector<uint8_t>& key) {
    // Read entire file
    std::ifstream in_file(path, std::ios::binary | std::ios::ate);
    if (!in_file) {
        Logger::error("[Crypto] Failed to open file for encryption: " + path);
        return false;
    }
    
    size_t file_size = in_file.tellg();
    in_file.seekg(0);
    
    std::vector<uint8_t> plaintext(file_size);
    in_file.read(reinterpret_cast<char*>(plaintext.data()), file_size);
    in_file.close();
    
    // Encrypt
    std::vector<uint8_t> ciphertext = encrypt(plaintext, key);
    if (ciphertext.empty()) {
        Logger::error("[Crypto] Encryption failed for file: " + path);
        return false;
    }
    
    // Write encrypted file with magic header
    std::string temp_path = path + ".enc";
    std::ofstream out_file(temp_path, std::ios::binary);
    if (!out_file) {
        Logger::error("[Crypto] Failed to create encrypted file: " + temp_path);
        return false;
    }
    
    out_file.write(reinterpret_cast<const char*>(MAGIC), sizeof(MAGIC));
    out_file.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());
    out_file.close();
    
    // Replace original with encrypted version
    try {
        fs::rename(temp_path, path);
        Logger::info("[Crypto] File encrypted successfully: " + path);
        return true;
    } catch (const std::exception& e) {
        Logger::error("[Crypto] Failed to replace file: " + std::string(e.what()));
        fs::remove(temp_path);
        return false;
    }
}

bool decrypt_file(const std::string& path, const std::vector<uint8_t>& key) {
    if (!is_encrypted_file(path)) {
        Logger::debug("[Crypto] File is not encrypted: " + path);
        return true;  // Not an error, just not encrypted
    }
    
    // Read entire file
    std::ifstream in_file(path, std::ios::binary | std::ios::ate);
    if (!in_file) {
        Logger::error("[Crypto] Failed to open file for decryption: " + path);
        return false;
    }
    
    size_t file_size = in_file.tellg();
    in_file.seekg(sizeof(MAGIC));  // Skip magic header
    
    std::vector<uint8_t> ciphertext(file_size - sizeof(MAGIC));
    in_file.read(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());
    in_file.close();
    
    // Decrypt
    std::vector<uint8_t> plaintext = decrypt(ciphertext, key);
    if (plaintext.empty()) {
        Logger::error("[Crypto] Decryption failed for file: " + path);
        return false;
    }
    
    // Write decrypted file
    std::string temp_path = path + ".dec";
    std::ofstream out_file(temp_path, std::ios::binary);
    if (!out_file) {
        Logger::error("[Crypto] Failed to create decrypted file: " + temp_path);
        return false;
    }
    
    out_file.write(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    out_file.close();
    
    // Replace encrypted with decrypted version
    try {
        fs::rename(temp_path, path);
        Logger::info("[Crypto] File decrypted successfully: " + path);
        return true;
    } catch (const std::exception& e) {
        Logger::error("[Crypto] Failed to replace file: " + std::string(e.what()));
        fs::remove(temp_path);
        return false;
    }
}

std::vector<uint8_t> hash_device_id(const std::string& device_id) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const uint8_t*>(device_id.c_str()), 
           device_id.length(), hash.data());
    return hash;
}

std::string get_keyfile_path() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.local/share/proton-drive/.keyfile";
}

bool store_encrypted_key(const std::vector<uint8_t>& key) {
    std::string keyfile_path = get_keyfile_path();
    if (keyfile_path.empty()) return false;
    
    // Create directory if needed
    fs::path dir = fs::path(keyfile_path).parent_path();
    fs::create_directories(dir);
    
    // Get machine-specific data for key derivation
    std::string machine_id;
    std::ifstream mid("/etc/machine-id");
    if (mid) {
        std::getline(mid, machine_id);
    }
    if (machine_id.empty()) {
        machine_id = "proton-drive-fallback";
    }
    
    // Derive encryption key from machine ID
    std::vector<uint8_t> salt = generate_salt();
    std::vector<uint8_t> derived_key = derive_key(machine_id, salt);
    if (derived_key.empty()) return false;
    
    // Encrypt the database key
    std::vector<uint8_t> encrypted_key = encrypt(key, derived_key);
    if (encrypted_key.empty()) return false;
    
    // Write: salt + encrypted key
    std::ofstream out(keyfile_path, std::ios::binary);
    if (!out) {
        Logger::error("[Crypto] Failed to create keyfile");
        return false;
    }
    
    out.write(reinterpret_cast<const char*>(salt.data()), salt.size());
    out.write(reinterpret_cast<const char*>(encrypted_key.data()), encrypted_key.size());
    out.close();
    
    // Set restrictive permissions (owner read/write only)
    fs::permissions(keyfile_path, 
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    
    Logger::info("[Crypto] Database key stored securely");
    return true;
}

std::vector<uint8_t> retrieve_encrypted_key() {
    std::string keyfile_path = get_keyfile_path();
    if (keyfile_path.empty()) return {};
    
    std::ifstream in(keyfile_path, std::ios::binary | std::ios::ate);
    if (!in) {
        Logger::debug("[Crypto] No keyfile found - will generate new key");
        return {};
    }
    
    size_t file_size = in.tellg();
    if (file_size < SALT_SIZE + IV_SIZE + TAG_SIZE) {
        Logger::warn("[Crypto] Keyfile corrupted or too small");
        return {};
    }
    
    in.seekg(0);
    
    // Read salt
    std::vector<uint8_t> salt(SALT_SIZE);
    in.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);
    
    // Read encrypted key
    std::vector<uint8_t> encrypted_key(file_size - SALT_SIZE);
    in.read(reinterpret_cast<char*>(encrypted_key.data()), encrypted_key.size());
    in.close();
    
    // Get machine-specific data for key derivation
    std::string machine_id;
    std::ifstream mid("/etc/machine-id");
    if (mid) {
        std::getline(mid, machine_id);
    }
    if (machine_id.empty()) {
        machine_id = "proton-drive-fallback";
    }
    
    // Derive decryption key
    std::vector<uint8_t> derived_key = derive_key(machine_id, salt);
    if (derived_key.empty()) return {};
    
    // Decrypt
    std::vector<uint8_t> key = decrypt(encrypted_key, derived_key);
    if (key.empty()) {
        Logger::warn("[Crypto] Failed to decrypt keyfile - machine ID may have changed");
    }
    
    return key;
}

} // namespace crypto
