#include <iostream>
#include <fstream>
#include <queue>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <functional>
#include <cstdint>

using namespace std;

struct Node {
    unsigned char byte;
    uint64_t frequency;
    shared_ptr<Node> left;
    shared_ptr<Node> right;

    Node(unsigned char b, uint64_t f)
        : byte(b), frequency(f) {}

    Node(shared_ptr<Node> l, shared_ptr<Node> r)
        : byte(0),
          frequency(l->frequency + r->frequency),
          left(move(l)),
          right(move(r)) {}

    bool isLeaf() const {
        return !left && !right;
    }
};

struct Compare {
    bool operator()(const shared_ptr<Node>& a,
                    const shared_ptr<Node>& b) const {
        return a->frequency > b->frequency;
    }
};

shared_ptr<Node> buildTree(
    const unordered_map<unsigned char, uint64_t>& frequency) {

    priority_queue<
        shared_ptr<Node>,
        vector<shared_ptr<Node>>,
        Compare
    > heap;

    for (const auto& [byte, freq] : frequency) {
        heap.push(make_shared<Node>(byte, freq));
    }

    if (heap.empty())
        return nullptr;

    while (heap.size() > 1) {
        auto left = heap.top();
        heap.pop();

        auto right = heap.top();
        heap.pop();

        heap.push(make_shared<Node>(left, right));
    }

    return heap.top();
}

void generateCodes(
    const shared_ptr<Node>& node,
    const string& code,
    unordered_map<unsigned char, string>& codes) {

    if (!node)
        return;

    if (node->isLeaf()) {
        codes[node->byte] = code.empty() ? "0" : code;
        return;
    }

    generateCodes(node->left, code + "0", codes);
    generateCodes(node->right, code + "1", codes);
}

class BitWriter {
private:
    ofstream& output;
    unsigned char buffer = 0;
    int bitCount = 0;

public:
    BitWriter(ofstream& out) : output(out) {}

    void writeBit(int bit) {
        buffer <<= 1;
        buffer |= (bit & 1);
        bitCount++;

        if (bitCount == 8) {
            output.put(static_cast<char>(buffer));
            buffer = 0;
            bitCount = 0;
        }
    }

    void writeCode(const string& code) {
        for (char bit : code)
            writeBit(bit - '0');
    }

    int flush() {
        if (bitCount == 0)
            return 0;

        buffer <<= (8 - bitCount);
        output.put(static_cast<char>(buffer));

        int validBits = bitCount;

        buffer = 0;
        bitCount = 0;

        return validBits;
    }
};

class BitReader {
private:
    ifstream& input;
    unsigned char buffer = 0;
    int bitsLeft = 0;

public:
    BitReader(ifstream& in) : input(in) {}

    int readBit() {
        if (bitsLeft == 0) {
            char c;

            if (!input.get(c))
                return -1;

            buffer = static_cast<unsigned char>(c);
            bitsLeft = 8;
        }

        int bit = (buffer >> (bitsLeft - 1)) & 1;
        bitsLeft--;

        return bit;
    }
};

bool compressFile(const string& inputName,
                  const string& outputName) {

    ifstream input(inputName, ios::binary);

    if (!input) {
        cerr << "Cannot open input file.\n";
        return false;
    }

    unordered_map<unsigned char, uint64_t> frequency;

    char c;

    while (input.get(c)) {
        unsigned char byte =
            static_cast<unsigned char>(c);

        frequency[byte]++;
    }

    input.clear();
    input.seekg(0);

    if (frequency.empty()) {
        ofstream output(outputName, ios::binary);

        if (!output)
            return false;

        uint16_t count = 0;
        output.write(
            reinterpret_cast<const char*>(&count),
            sizeof(count)
        );

        return true;
    }

    auto root = buildTree(frequency);

    unordered_map<unsigned char, string> codes;
    generateCodes(root, "", codes);

    ofstream output(outputName, ios::binary);

    if (!output) {
        cerr << "Cannot create output file.\n";
        return false;
    }

    uint16_t symbolCount =
        static_cast<uint16_t>(frequency.size());

    output.write(
        reinterpret_cast<const char*>(&symbolCount),
        sizeof(symbolCount)
    );

    for (const auto& [byte, freq] : frequency) {
        output.write(
            reinterpret_cast<const char*>(&byte),
            sizeof(byte)
        );

        output.write(
            reinterpret_cast<const char*>(&freq),
            sizeof(freq)
        );
    }

    uint64_t originalSize = 0;

    for (const auto& [byte, freq] : frequency)
        originalSize += freq;

    output.write(
        reinterpret_cast<const char*>(&originalSize),
        sizeof(originalSize)
    );

    BitWriter writer(output);

    while (input.get(c)) {
        unsigned char byte =
            static_cast<unsigned char>(c);

        writer.writeCode(codes[byte]);
    }

    int validBits = writer.flush();

    uint8_t finalBits =
        static_cast<uint8_t>(validBits);

    output.write(
        reinterpret_cast<const char*>(&finalBits),
        sizeof(finalBits)
    );

    output.close();
    input.close();

    return true;
}

bool decompressFile(const string& inputName,
                    const string& outputName) {

    ifstream input(inputName, ios::binary);

    if (!input) {
        cerr << "Cannot open compressed file.\n";
        return false;
    }

    uint16_t symbolCount;

    input.read(
        reinterpret_cast<char*>(&symbolCount),
        sizeof(symbolCount)
    );

    if (!input)
        return false;

    unordered_map<unsigned char, uint64_t> frequency;

    for (uint16_t i = 0; i < symbolCount; i++) {

        unsigned char byte;
        uint64_t freq;

        input.read(
            reinterpret_cast<char*>(&byte),
            sizeof(byte)
        );

        input.read(
            reinterpret_cast<char*>(&freq),
            sizeof(freq)
        );

        frequency[byte] = freq;
    }

    uint64_t originalSize;

    input.read(
        reinterpret_cast<char*>(&originalSize),
        sizeof(originalSize)
    );

    if (!input)
        return false;

    ofstream output(outputName, ios::binary);

    if (!output) {
        cerr << "Cannot create output file.\n";
        return false;
    }

    if (symbolCount == 0)
        return true;

    auto root = buildTree(frequency);

    BitReader reader(input);

    auto current = root;

    uint64_t decoded = 0;

    while (decoded < originalSize) {

        int bit = reader.readBit();

        if (bit == -1)
            return false;

        if (bit == 0)
            current = current->left;
        else
            current = current->right;

        if (current->isLeaf()) {

            output.put(
                static_cast<char>(current->byte)
            );

            decoded++;

            current = root;
        }
    }

    output.close();
    input.close();

    return true;
}

int main(int argc, char* argv[]) {

    if (argc != 4) {
        cout << "Usage:\n";
        cout << "  huffman compress input output\n";
        cout << "  huffman decompress input output\n";
        return 1;
    }

    string mode = argv[1];
    string input = argv[2];
    string output = argv[3];

    if (mode == "compress") {

        if (compressFile(input, output))
            cout << "Compression successful.\n";
        else
            cout << "Compression failed.\n";

    } else if (mode == "decompress") {

        if (decompressFile(input, output))
            cout << "Decompression successful.\n";
        else
            cout << "Decompression failed.\n";

    } else {
        cout << "Unknown operation.\n";
        return 1;
    }

    return 0;
}
