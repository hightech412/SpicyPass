/*  spicy.cpp
 *
 *
 *  Copyright (C) 2020 Jfreegman <Jfreegman@gmail.com>
 *
 *  This file is part of SpicyPass.
 *
 *  SpicyPass is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SpicyPass is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with SpicyPass.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <SpicyPassConfig.h>

#include <thread>
#include <chrono>

#include <sys/types.h>
#include <sys/stat.h>

#include "load.hpp"
#include "password.hpp"
#include "spicy.hpp"
#include "util.hpp"
#include "crypto.hpp"

using namespace std;

typedef enum {
    OPT_EXIT = 0,
    OPT_ADD,
    OPT_REMOVE,
    OPT_FETCH,
    OPT_LIST,
    OPT_GENERATE,
    OPT_PASSWORD,
    OPT_PRINT,
} Options;


/* Prompts password and puts it in `password` array.
 *
 * Return 0 on success.
 * Return -1 input is invalid.
 */
static int prompt_password(unsigned char *password, size_t max_length)
{
    cout << "Enter password: ";

    char pass_buf[MAX_STORE_PASSWORD_SIZE + 1];
    const char *input = fgets(pass_buf, sizeof(pass_buf), stdin);

    if (input == NULL) {
        cout << "Invalid input." << endl;
        return -1;
    }

    size_t pass_length = strlen(pass_buf);

    if (pass_length > max_length) {
        return -1;
    }

    memcpy(password, pass_buf, pass_length);
    password[pass_length] = 0;

    crypto_memwipe((unsigned char *) pass_buf, sizeof(pass_buf));

    return 0;
}

static void new_password_prompt(unsigned char *password, size_t max_length)
{
    while (true) {
        cout << "Enter password: ";

        char pass1[MAX_STORE_PASSWORD_SIZE + 1];
        char pass2[MAX_STORE_PASSWORD_SIZE + 1];

        const char *input1 = fgets(pass1, sizeof(pass1), stdin);

        if (input1 == NULL) {
            cout << "Invalid input." << endl;
            continue;
        }

        size_t pass_length = strlen(pass1);

        if (pass_length < MIN_STORE_PASSWORD_SIZE || pass_length > max_length) {
            cout << "Password must be between " << MIN_STORE_PASSWORD_SIZE  << " and " << max_length << " characters long." << endl;
            continue;
        }

        cout << "Enter password again: ";

        const char *input2 = fgets(pass2, sizeof(pass2), stdin);

        if (input2 == NULL) {
            cout << "Invalid input." << endl;
            continue;
        }

        if (strcmp(pass1, pass2) != 0) {
            cout << "Passwords don't match. Try again." << endl;
            continue;
        }

        memcpy(password, pass1, pass_length);
        password[pass_length] = 0;

        crypto_memwipe((unsigned char *) pass1, sizeof(pass1));
        crypto_memwipe((unsigned char *) pass2, sizeof(pass2));

        return;
    }
}

/*
 * Initializes pass store file with password hash on first run. Puts new password in
 * the `password` buffer.
 *
 * Returns 0 on success.
 * Returns -1 on failure.
 */
static int init_new_password(unsigned char *password, size_t max_length)
{
    struct termios oflags;
    disable_terminal_echo(&oflags);

    new_password_prompt(password, max_length);

    enable_terminal_echo(&oflags);

    if (init_pass_hash(password, strlen((char *) password)) != 0) {
        cout << "init_pass_hash() failed." << endl;
        return -1;
    }

    return 0;
}

/*
 * Prompts user to update password for pass store file.
 *
 * Return 0 on success.
 * Return -1 on failure.
 * Return PASS_STORE_LOCKED if pass store is locked.
 */
static int change_password_prompt(Pass_Store &p)
{
    unsigned char new_password[MAX_STORE_PASSWORD_SIZE + 1];
    unsigned char hash[CRYPTO_HASH_SIZE];
    p.get_password_hash(hash);

    cout << "Changing master password. Enter q to go back." << endl;

    while (true) {
        cout << "Enter old password: ";

        char old_pass[MAX_STORE_PASSWORD_SIZE + 1];
        const char *input1 = fgets(old_pass, sizeof(old_pass), stdin);

        if (input1 == NULL) {
            cout << "Invalid input" << endl;
            continue;
        }

        if (strcmp(old_pass, "q\n") == 0) {
            return -1;
        }

        size_t pass_length = strlen(old_pass);

        if (!crypto_verify_pass_hash(hash, (unsigned char *) old_pass, pass_length)) {
            cout << "Invalid password" << endl;
            continue;
        }

        break;
    }

    new_password_prompt(new_password, MAX_STORE_PASSWORD_SIZE);
    int ret = update_crypto(p, new_password, strlen((char *) new_password));

    if (ret == PASS_STORE_LOCKED) {
        return PASS_STORE_LOCKED;
    }

    if (ret < 0) {
        cout << "Failed to update password (error code: " << to_string(ret) << ")" << endl;
        return -1;
    }

    cout << "Successfully updated password" << endl;

    crypto_memwipe(new_password, sizeof(new_password));

    return 0;
}

static int new_password(Pass_Store &p)
{
    struct termios oflags;
    disable_terminal_echo(&oflags);

    int ret = change_password_prompt(p);

    enable_terminal_echo(&oflags);

    return ret;
}

static int add(Pass_Store &p)
{
    string key, password;

    cout << "Enter key to add: ";
    getline(cin, key);

    if (key.length() > MAX_ENTRY_KEY_SIZE) {
        cout << "Key is too long" << endl;
        return -1;
    }

    if (key.length() == 0) {
        cout << "Invalid key" << endl;
        return -1;
    }

    if (string_contains(key, DELIMITER)) {
        cout << "Key may not contain the \"" << DELIMITER << "\" character" << endl;
        return -1;
    }

    cout << "Enter password (leave empty for a random password): ";
    getline(cin, password);

    if (password.length() > MAX_STORE_PASSWORD_SIZE) {
        cout << "Password length must not exceed " << to_string(MAX_STORE_PASSWORD_SIZE) << " characters" << endl;
        return -1;
    }

    if (password.empty()) {
        password = random_password(16U);
    }

    if (password.empty()) {
        cout << "Failed to add entry" << endl;
        return -1;
    }

    int exists = p.key_exists(key);

    if (exists == PASS_STORE_LOCKED) {
        return PASS_STORE_LOCKED;
    }

    if (exists > 0) {
        while (true) {
            string s;
            cout << "Key \"" << key << "\" already exists. Overwrite? Y/n ";
            getline(cin, s);

            if (s == "Y") {
                break;
            } else if (s == "n") {
                return 0;
            }
        }
    }

    if (p.insert(key, password) != 0) {
        cout << "Failed to add entry" << endl;
        return -1;
    }

    int ret = save_password_store(p);

    switch (ret) {
        case 0: {
            cout << "Added key " << key << " with password " << password << endl;
            return 0;
        }
        case -1: {
            cout << "Failed to save password store: Failed to open pass store file" << endl;
            return -1;
        }
        case -2: {
            cout << "Failed to save password store: Encryption error" << endl;
            return -1;

        }
        case -3: {
            cout << "Failed to save password store: File save error" << endl;
            return -1;
        }
        default: {
            cout << "Failed to save password store: Unknown error" << endl;
            return -1;
        }
    }

    return 0;
}

static int remove(Pass_Store &p)
{
    string key;
    cout << "Enter key to remove: ";
    getline(cin, key);

    while (true) {
        cout << "Are you sure you want to remove the key \"" << key << "\" ? Y/n ";
        string s;
        getline(cin, s);

        if (s == "Y") {
            break;
        } else if (s == "n") {
            return 0;
        }
    }

    int removed = p.remove(key);

    if (removed == PASS_STORE_LOCKED) {
        return PASS_STORE_LOCKED;
    }

    if (removed != 0) {
        cout << "Key not found" << endl;
        return -1;
    }

    cout << "Removed entry for key \"" << key << "\"" << endl;

    int ret = save_password_store(p);

    if (ret != 0) {
        cout << "Failed to save password store (error code: " << to_string(ret) << ")" << endl;
        return -1;
    }

    return 0;
}

static int fetch(Pass_Store &p)
{
    cout << "Enter key: ";

    string key;
    getline(cin, key);

    if (key.empty()) {
        return -1;
    }

    int matches = p.print_matches(key, true);

    if (matches == PASS_STORE_LOCKED) {
        return PASS_STORE_LOCKED;
    }

    if (!matches) {
        cout << "Key not found" << endl;
        return -1;
    }

    return 0;
}

static int list(Pass_Store &p)
{
    int matches = p.print_matches("", false);

    if (matches == PASS_STORE_LOCKED) {
        return PASS_STORE_LOCKED;
    }

    return 0;
}

static void generate(void)
{
    string input;
    int size = 0;

    while (true) {
        cout << "Enter password length: ";
        getline(cin, input);

        try {
            size = stoi(input);
        } catch (const exception &) {
            cout << "Invalid input" << endl;
            continue;
        }

        if (size >= MIN_STORE_PASSWORD_SIZE && size <= MAX_STORE_PASSWORD_SIZE) {
            break;
        }

        cout << "Password must be between " << to_string(MIN_STORE_PASSWORD_SIZE) << " and " << to_string(MAX_STORE_PASSWORD_SIZE) << " characters in length" << endl;
    }

    string pass = random_password(size);

    if (pass.empty()) {
        cout << "Failed to generate password" << endl;
        return;
    }

    cout << pass << endl;
}

static bool unlock_prompt(Pass_Store &p)
{
    cout << "Enter password: ";

    unsigned char pass[MAX_STORE_PASSWORD_SIZE + 1];
    const char *input = fgets((char *) pass, sizeof(pass), stdin);

    if (input == NULL) {
        cout << "Invalid input" << endl;
        return false;
    }

    int ret = load_password_store(p, pass, strlen((char *) pass));

    crypto_memwipe(pass, sizeof(pass));

    switch (ret) {
        case 0: {
            return true;
        }
        case -2: {
            cout << "Invalid password" << endl;
            break;
        }
        default: {
            break;
        }
    }

    return false;
}

void lock_check(Pass_Store &p)
{
    struct termios oflags;
    disable_terminal_echo(&oflags);

    cout << "Idle lock has been activated. ";

    while (!unlock_prompt(p))
        ;

    enable_terminal_echo(&oflags);
}

static void print_menu(void)
{
    cout << "Menu:" << endl;
    cout << "[" << to_string(OPT_ADD) << "] Add entry" << endl;
    cout << "[" << to_string(OPT_REMOVE) << "] Remove entry" << endl;
    cout << "[" << to_string(OPT_FETCH) << "] Fetch entry" << endl;
    cout << "[" << to_string(OPT_LIST) << "] List all entries" << endl;
    cout << "[" << to_string(OPT_GENERATE) << "] Generate password" << endl;
    cout << "[" << to_string(OPT_PASSWORD) << "] Change master password" << endl;
    cout << "[" << to_string(OPT_PRINT) << "] Print menu" << endl;
    cout << "[" << to_string(OPT_EXIT) << "] Exit" << endl;
}

/*
 * Executes command indicated by `option`.
 *
 * Return 0 on normal execution (including errors).
 * Return -1 on exit command.
 * Return PASS_STORE_LOCKED if pass store is locked.
 */
static int execute(const int option, Pass_Store &p)
{
    if (option == OPT_EXIT) {
        return -1;
    }

    if (p.check_lock()) {
        return PASS_STORE_LOCKED;
    }

    int ret = 0;

    switch (option) {
        case OPT_ADD: {
            ret = add(p);
            break;
        }
        case OPT_REMOVE: {
            ret = remove(p);
            break;
        }
        case OPT_FETCH: {
            ret = fetch(p);
            break;
        }
        case OPT_LIST: {
            ret = list(p);
            break;
        }
        case OPT_GENERATE: {
            generate();
            break;
        }
        case OPT_PASSWORD: {
            ret = new_password(p);
            break;
        }
        case OPT_PRINT: {
            clear_console();
            print_menu();
            break;
        }
        default: {
            cout << "Invalid command" << endl;
            print_menu();
            break;
        }
    }

    return (ret != PASS_STORE_LOCKED) ? 0 : PASS_STORE_LOCKED;
}

static int command_prompt(void)
{
    cout << "> ";
    string prompt;
    getline(cin, prompt);

    try {
        return stoi(prompt);
    } catch (const exception &) {
        return -1;
    }
}

static void menu_loop(Pass_Store &p)
{
    print_menu();

    while (true) {
        int option = command_prompt();
        int ret = execute(option, p);

        switch (ret) {
            case 0: {
                break;
            }
            case PASS_STORE_LOCKED: {
                lock_check(p);
                break;
            }
            default: {
                return;
            }
        }
    }
}

/*
 * Initializes a new `Pass_Store` object and prompts user for password.
 *
 * Return 0 on success.
 * Return -1 if password prompt fails.
 * Return -2 if memory lock fails.
 * Return -3 if pass store file could not be opened.
 * Return -4 on invalid password.
 * Return -5 on decryption error.
 */
int new_pass_store(Pass_Store &p)
{
    unsigned char password[MAX_STORE_PASSWORD_SIZE + 1];

    if (first_time_run()) {
        cout << "Creating a new profile. ";

        if (init_new_password(password, MAX_STORE_PASSWORD_SIZE) != 0) {
            return -1;
        }
    } else {
        struct termios oflags;
        disable_terminal_echo(&oflags);

        int pw_ret = prompt_password(password, MAX_STORE_PASSWORD_SIZE);

        enable_terminal_echo(&oflags);

        if (pw_ret != 0) {
            return -1;
        }
    }

    int ret = load_password_store(p, password, strlen((char *) password));

    crypto_memwipe(password, sizeof(password));

    switch (ret) {
        case 0: {
            break;
        }
        case -1: {
            return -3;
        }
        case -2: {
            return -4;
        }
        case -3: {
            return -5;
        }
        case -4: {
            return -3;
        }
        default: {
            return -3;
        }
    }

    return 0;
}

static void print_version(const char *binary_name)
{
    cout << binary_name << " version "
         << SpicyPass_VERSION_MAJOR << "."
         << SpicyPass_VERSION_MINOR << "."
         << SpicyPass_VERSION_PATCH << endl;
}

void store_lock_loop(Pass_Store &p)
{
    while(true) {
        p.poll_idle();
        this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main(int argc, char **argv)
{
    if (argc > 0) {
        print_version(argv[0]);
    }

    umask(S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    if (crypto_init() != 0) {
        cout << "crypto_init() failed" << endl;
        return -1;
    }

    Pass_Store p;
    int ret = new_pass_store(p);

    switch (ret) {
        case 0: {
            break;
        }
        case -1: {
            return -1;
        }
        case -2: {
            cout << "crypto_memlock() failed in new_pass_store()" << endl;
            return -1;
        }
        case -3: {
            cout << "load_password_store() failed to open pass store file" << endl;
            return -1;
        }
        case -4: {
            cout << "Invalid password" << endl;
            return -1;
        }
        case -5: {
            cout << "Failed to decrypt pass store file" << endl;
            return -1;
        }
        default: {
            cout << "Unknown error" << endl;
            return -1;
        }
    }

    thread t(store_lock_loop, ref(p));
    t.detach();

    menu_loop(p);

    clear_console();

    return 0;
}