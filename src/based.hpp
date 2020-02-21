/*  based.hpp
 *
 *
 *  Copyright (C) 2019 Jfreegman <Jfreegman@gmail.com>
 *
 *  This file is part of BasedPass.
 *
 *  BasedPass is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  BasedPass is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with BasedPass.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BASED
#define BASED

#include <iostream>
#include <string>
#include <fstream>
#include <map>

#include <string.h>

#include "load.hpp"
#include "util.hpp"
#include "crypto.hpp"

#define DELIMITER ":"

using namespace std;

class Pass_Store {
    map<string, string> store;

    unsigned char encryption_key[CRYPTO_KEY_SIZE];
    unsigned char key_salt[CRYPTO_SALT_SIZE];
    unsigned char password_hash[CRYPTO_HASH_SIZE];

public:
    void insert(string key, string value) {
        store.insert_or_assign(key, value);
    }

    void remove(string key) {
        store.erase(key);
    }

    bool key_exists(string key) {
        return store.find(key) != store.end();
    }

    bool print_matches(string key) {
        bool match = false;

        for (auto &item: store) {
            if (key.compare(0, key.length(), item.first, 0, key.length()) == 0) {
                cout << item.first << ": " << item.second << endl;
                match = true;
            }
        }

        return match;
    }

    /*
     * Decrypts file pointed to by `fp` and loads conents to pass store.
     *
     * Return -1 on out of memory error.
     * Return -2 on decryption error.
     */
    int load(ifstream &fp, size_t file_size) {
        unsigned char *plaintext = (unsigned char *) malloc(file_size + 1);
        unsigned long long plain_length;

        int ret = crypto_decrypt_file(fp, file_size, plaintext, &plain_length, encryption_key);

        if (ret != 0) {
            free(plaintext);

            switch (ret) {
                case -1: {
                    cout << "Decryption failed: Out of memory" << endl;
                    return -2;
                }
                case -2: {
                    cout << "Decryption failed: Corrupt file or bad key" << endl;
                    return -2;
                }
                case -3: {
                    cout << "Decryption failed: File corrupt" << endl;
                    return -2;
                }
                default: {
                    return -2;
                }
            }
        }

        plaintext[plain_length] = 0;

        char *tok = strtok((char *) plaintext, "\n");

        while (tok) {
            string entry = tok;
            unsigned int d = entry.find(DELIMITER);

            if (d != string::npos) {
                string key = entry.substr(0, d);
                string pass = entry.substr(d + 1, entry.length());
                insert(key, pass);
            }

            tok = strtok(NULL, "\n");
        }

        crypto_memwipe((char *) plaintext, plain_length);

        free(plaintext);

        return 0;
    }

    /*
     * Encrypts pass store data and saves result to file pointed to by `fp`.
     * fp should be offset to after the plaintext header.
     *
     * Return 0 on success.
     * Return -1 on memory related error.
     * Return -2 if encryption fails.
     */
    int save(ofstream &fp) {
        size_t file_size = 0;

        for (auto &item: store) {
            string entry = item.first + DELIMITER + item.second + '\n';
            file_size += entry.length();
        }

        if (file_size == 0) {
            return 0;
        }

        unsigned char *buf_in = (unsigned char *) malloc(file_size);

        if (buf_in == NULL) {
            return -1;
        }

        size_t pos = 0;

        for (auto &item: store) {
            string entry = item.first + DELIMITER + item.second + '\n';
            memcpy(buf_in + pos, entry.c_str(), entry.length());
            pos += entry.length();
        }

        unsigned long long out_len;
        int ret = crypto_encrypt_file(fp, buf_in, file_size, &out_len, encryption_key);

        if (ret < 0) {
            cout << "Encryption failed" << endl;
            free(buf_in);
            return -2;
        }

        crypto_memwipe((char *) buf_in, file_size);

        free(buf_in);

        return 0;
    }

    void kill(void) {
        crypto_memunlock((char *) encryption_key, CRYPTO_KEY_SIZE);
    }

    int init_crypto(const char *key, const char *salt, const char *hash) {
        memcpy(encryption_key, key, CRYPTO_KEY_SIZE);
        memcpy(key_salt, salt, CRYPTO_SALT_SIZE);
        memcpy(password_hash, hash, CRYPTO_HASH_SIZE);

        if (crypto_memlock((char *) encryption_key, CRYPTO_KEY_SIZE) != 0) {
            return -1;
        }

        return 0;
    }

    void get_key_salt(char *buf) {
        memcpy(buf, key_salt, CRYPTO_SALT_SIZE);
    }

    void get_password_hash(char *buf) {
        memcpy(buf, password_hash, CRYPTO_HASH_SIZE);
    }
};

#endif