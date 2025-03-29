// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <memory>

#include "parquet/encryption/encryption.h"
#include "parquet/encryption/file_key_wrapper.h"
#include "parquet/encryption/key_toolkit.h"
#include "parquet/encryption/kms_client_factory.h"
#include "parquet/platform.h"

namespace parquet::encryption {

static constexpr ParquetCipher::type kDefaultEncryptionAlgorithm =
    ParquetCipher::AES_GCM_V1;
static constexpr bool kDefaultPlaintextFooter = false;
static constexpr bool kDefaultDoubleWrapping = true;
static constexpr double kDefaultCacheLifetimeSeconds = 600;  // 10 minutes
static constexpr bool kDefaultInternalKeyMaterial = true;
static constexpr bool kDefaultUniformEncryption = false;
static constexpr int32_t kDefaultDataKeyLengthBits = 128;

struct PARQUET_EXPORT EncryptionConfiguration {
  explicit EncryptionConfiguration(const std::string& footer_key)
      : footer_key(footer_key) {}

  /// ID of the master key for footer encryption/signing
  std::string footer_key;

  /// List of columns to encrypt, with column master key IDs (see HIVE-21848).
  /// Format: "columnKeyID:colName,colName;columnKeyID:colName..."
  /// Either
  /// (1) column_keys must be set
  /// or
  /// (2) uniform_encryption must be set to true
  /// If none of (1) and (2) are true, or if both are true, an exception will be
  /// thrown.
  std::string column_keys;

  /// Encrypt footer and all columns with the same encryption key.
  bool uniform_encryption = kDefaultUniformEncryption;

  /// Parquet encryption algorithm. Can be "AES_GCM_V1" (default), or "AES_GCM_CTR_V1".
  ParquetCipher::type encryption_algorithm = kDefaultEncryptionAlgorithm;

  /// Write files with plaintext footer.
  /// The default is false - files are written with encrypted footer.
  bool plaintext_footer = kDefaultPlaintextFooter;

  /// Use double wrapping - where data encryption keys (DEKs) are encrypted with key
  /// encryption keys (KEKs), which in turn are encrypted with master keys.
  /// The default is true. If set to false, use single wrapping - where DEKs are
  /// encrypted directly with master keys.
  bool double_wrapping = kDefaultDoubleWrapping;

  /// Lifetime of cached entities (key encryption keys, local wrapping keys, KMS client
  /// objects).
  /// The default is 600 (10 minutes).
  double cache_lifetime_seconds = kDefaultCacheLifetimeSeconds;

  /// Store key material inside Parquet file footers; this mode doesn’t produce
  /// additional files. By default, true. If set to false, key material is stored in
  /// separate files in the same folder, which enables key rotation for immutable
  /// Parquet files.
  bool internal_key_material = kDefaultInternalKeyMaterial;

  /// Length of data encryption keys (DEKs), randomly generated by parquet key
  /// management tools. Can be 128, 192 or 256 bits.
  /// The default is 128 bits.
  int32_t data_key_length_bits = kDefaultDataKeyLengthBits;
};

struct PARQUET_EXPORT DecryptionConfiguration {
  /// Lifetime of cached entities (key encryption keys, local wrapping keys, KMS client
  /// objects).
  /// The default is 600 (10 minutes).
  double cache_lifetime_seconds = kDefaultCacheLifetimeSeconds;
};

/// This is a core class, that translates the parameters of high level encryption (like
/// the names of encrypted columns, names of master keys, etc), into parameters of low
/// level encryption (like the key metadata, DEK, etc). A factory that produces the low
/// level FileEncryptionProperties and FileDecryptionProperties objects, from the high
/// level parameters.
class PARQUET_EXPORT CryptoFactory {
 public:
  /// a KmsClientFactory object must be registered via this method before calling any of
  /// GetFileEncryptionProperties()/GetFileDecryptionProperties() methods.
  void RegisterKmsClientFactory(std::shared_ptr<KmsClientFactory> kms_client_factory);

  /// Get the encryption properties for a Parquet file.
  /// If external key material is used then a file system and path to the
  /// parquet file must be provided.
  std::shared_ptr<FileEncryptionProperties> GetFileEncryptionProperties(
      const KmsConnectionConfig& kms_connection_config,
      const EncryptionConfiguration& encryption_config, const std::string& file_path = "",
      const std::shared_ptr<::arrow::fs::FileSystem>& file_system = NULLPTR);

  /// Get decryption properties for a Parquet file.
  /// If external key material is used then a file system and path to the
  /// parquet file must be provided.
  std::shared_ptr<FileDecryptionProperties> GetFileDecryptionProperties(
      const KmsConnectionConfig& kms_connection_config,
      const DecryptionConfiguration& decryption_config, const std::string& file_path = "",
      const std::shared_ptr<::arrow::fs::FileSystem>& file_system = NULLPTR);

  void RemoveCacheEntriesForToken(const std::string& access_token) {
    key_toolkit_->RemoveCacheEntriesForToken(access_token);
  }

  void RemoveCacheEntriesForAllTokens() {
    key_toolkit_->RemoveCacheEntriesForAllTokens();
  }

  /// Rotates master encryption keys for a Parquet file that uses external key material.
  /// In single wrapping mode, data encryption keys are decrypted with the old master keys
  /// and then re-encrypted with new master keys.
  /// In double wrapping mode, key encryption keys are decrypted with the old master keys
  /// and then re-encrypted with new master keys.
  /// This relies on the KMS supporting versioning, such that the old master key is
  /// used when unwrapping a key, and the latest version is used when wrapping a key.
  void RotateMasterKeys(const KmsConnectionConfig& kms_connection_config,
                        const std::string& parquet_file_path,
                        const std::shared_ptr<::arrow::fs::FileSystem>& file_system,
                        bool double_wrapping = kDefaultDoubleWrapping,
                        double cache_lifetime_seconds = kDefaultCacheLifetimeSeconds);

 private:
  ColumnPathToEncryptionPropertiesMap GetColumnEncryptionProperties(
      int dek_length, const std::string& column_keys, FileKeyWrapper* key_wrapper);

  /// Key utilities object for kms client initialization and cache control
  std::shared_ptr<KeyToolkit> key_toolkit_ = std::make_shared<KeyToolkit>();
};

}  // namespace parquet::encryption
