#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

struct ChunkHeader { char id[4]; uint32_t size; };

// original fmt chunk buffer
struct FmtChunk {
    uint32_t fmt_size;
    std::vector<uint8_t> data; // exactly fmt_size bytes
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
};

bool readWavFile(const std::string& filename,
    FmtChunk& fmt,
    uint32_t& data_size,
    std::vector<int16_t>& audioData)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) { std::cerr << "Cannot open " << filename << "\n"; return false; }

    char riff[4], wave[4];
    uint32_t riff_size;
    file.read(riff, 4); file.read(reinterpret_cast<char*>(&riff_size), 4);
    file.read(wave, 4);
    if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        std::cerr << "Not a valid WAV file\n"; return false;
    }

    bool sawFmt = false, sawData = false;
    ChunkHeader ch;
    while (file.read(reinterpret_cast<char*>(&ch), sizeof(ch))) {
        std::string cid(ch.id, 4);
        if (cid == "fmt ") {
            sawFmt = true;
            fmt.fmt_size = ch.size;
            fmt.data.resize(fmt.fmt_size);
            file.read(reinterpret_cast<char*>(fmt.data.data()), fmt.fmt_size);
            // parse key fields
            memcpy(&fmt.audio_format, fmt.data.data() + 0, sizeof(uint16_t));
            memcpy(&fmt.num_channels, fmt.data.data() + 2, sizeof(uint16_t));
            memcpy(&fmt.sample_rate, fmt.data.data() + 4, sizeof(uint32_t));
            memcpy(&fmt.bits_per_sample, fmt.data.data() + 14, sizeof(uint16_t));
        }
        else if (cid == "data") {
            sawData = true;
            data_size = ch.size;
            break;
        }
        else {
            // skip optional chunk
            file.seekg(ch.size, std::ios::cur);
        }
    }

    if (!sawFmt || !sawData || fmt.audio_format != 1) {
        std::cerr << "Invalid WAV structure (missing fmt, missing data, or non‑PCM)\n";
        return false;
    }
    if (fmt.bits_per_sample != 16) {
        std::cerr << "Only 16‑bit WAV supported\n"; return false;
    }

    uint32_t num_samples = data_size / (fmt.num_channels * sizeof(int16_t));
    audioData.resize(num_samples * fmt.num_channels);
    file.read(reinterpret_cast<char*>(audioData.data()), data_size);
    return true;
}

void applyPreEmphasis(std::vector<int16_t>& audioData,
    int numChannels,
    float alpha = 0.97f)
{
    //FIR, not IIR
    for (int ch = 0; ch < numChannels; ++ch) {
        float prev = 0.0f;
        for (size_t i = ch; i < audioData.size(); i += numChannels) {
            float cur = audioData[i];
            float em = cur - alpha * prev;
            prev = cur;
            if (em > 32767.f) em = 32767.f;
            if (em < -32768.f) em = -32768.f;
            audioData[i] = static_cast<int16_t>(em);
        }
    }
}

bool writeWavFile(const std::string& filename,
    const FmtChunk& fmt,
    const std::vector<int16_t>& audioData)
{
    uint32_t data_size = audioData.size() * sizeof(int16_t);
    uint32_t riff_size = 4 /*"WAVE"*/ + 8 + fmt.fmt_size + 8 + data_size;

    std::ofstream o(filename, std::ios::binary);
    if (!o) { std::cerr << "Cannot open " << filename << " to write\n"; return false; }

    o.write("RIFF", 4);
    o.write(reinterpret_cast<const char*>(&riff_size), 4);
    o.write("WAVE", 4);

    o.write("fmt ", 4);
    o.write(reinterpret_cast<const char*>(&fmt.fmt_size), 4);
    o.write(reinterpret_cast<const char*>(fmt.data.data()), fmt.fmt_size);

    o.write("data", 4);
    o.write(reinterpret_cast<const char*>(&data_size), 4);
    o.write(reinterpret_cast<const char*>(audioData.data()), data_size);

    return true;
}

int main() {
    const std::string inputFile = "test.wav";
    const std::string outputFile = "output_preemphasized.wav";

    FmtChunk fmt;
    uint32_t data_size;
    std::vector<int16_t> audioData;

    if (!readWavFile(inputFile, fmt, data_size, audioData)) {
        std::cerr << "Failed to read input\n"; return 1;
    }

    applyPreEmphasis(audioData, fmt.num_channels, 0.97f);

    if (!writeWavFile(outputFile, fmt, audioData)) {
        std::cerr << "Failed to write output\n"; return 2;
    }

    std::cout << "Wrote " << outputFile << " successfully.\n";
    return 0;
}
