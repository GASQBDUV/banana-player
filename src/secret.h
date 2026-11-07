// Sealing saved passwords (AES-256-GCM). It keeps them from being read off the card
// by eye or a backup, not from someone with the whole card.
#pragma once
#include <string>

namespace bp
{
	// 32 random bytes in keyFile (mode 0600, written atomically); made if missing.
	bool loadOrCreateKey(const std::string& keyFile, std::string& key32);
	// This device's serial, from the device tree; "" on a computer (the key then rests on the file alone).
	std::string deviceSerial();
	// HKDF-SHA256 of the file's key, salted with the serial: a copied card alone can't open the passwords.
	std::string deriveKey(const std::string& fileKey, const std::string& serial);

	// "v1:" + base64(nonce 12 + ciphertext + tag 16); "" if it can't.
	std::string sealSecret(const std::string& plain, const std::string& key32);
	// False on a wrong key, a changed byte or a bad format.
	bool openSecret(const std::string& sealed, const std::string& key32, std::string& plain);

	std::string base64Encode(const std::string& bytes);
	bool base64Decode(const std::string& text, std::string& bytes);
}
