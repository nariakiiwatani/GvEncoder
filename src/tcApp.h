#pragma once

#include <TrussC.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "GpuVideo.hpp"
#include "GpuVideoIO.hpp"
#include "GpuVideoReader.hpp"
#include "squish.h"
#include "lz4.h"
#include "lz4hc.h"

using namespace std;
using namespace tc;

// Access command line args from main.cpp
int getArgCount();
char** getArgValues();

enum class InputKind {
    None,
    ImageSequence,
    VideoFile
};

enum class Codec {
    DXT1,
    DXT3,
    DXT5
};

struct EncodeSettings {
    Codec codec = Codec::DXT1;
    float fps = 30.0f;
    bool fpsSpecified = false;
    bool resizeEnabled = false;
    int resizeWidth = 0;
    int resizeHeight = 0;
    std::filesystem::path outputPath;
    bool deleteSource = false;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;

    void keyPressed(int key) override;
    void keyReleased(int key) override;

    void mousePressed(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void mouseMoved(Vec2 pos) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseScrolled(Vec2 delta) override;

    void windowResized(int width, int height) override;
    void filesDropped(const vector<string>& files) override;
    void exit() override;

private:
    void parseCommandLine();
    void resetState();
    void clearInput();
    bool setInputPath(const std::filesystem::path& path);
    bool enqueueInputPath(const std::filesystem::path& path, const std::string& displayLabel = "");
    bool dequeueNextInput();
    bool startConversion();
    void processBatch();
    void finishConversion(bool success);
    bool prepareInput();
    bool prepareVideoIntermediate();
    bool buildImageList(const std::filesystem::path& directory);
    bool isImageExtension(const std::filesystem::path& path) const;
    std::vector<std::filesystem::path> collectImageDirectories(const std::filesystem::path& root) const;
    std::filesystem::path resolveOutputPath() const;
    float detectFpsFromMeta(const std::filesystem::path& directory) const;
    std::string runCommandCapture(const std::string& cmd, int& exitCode) const;
    bool runCommand(const std::string& cmd, int& exitCode) const;
    std::string quotePath(const std::filesystem::path& path) const;
    Pixels resizeNearest(const Pixels& src, int width, int height) const;
    void cleanupIntermediate();
    void drawGui();
    void loadGuiSettings();
    void saveGuiSettings() const;
    std::filesystem::path getSettingsPath() const;
    std::string buildCommandLine() const;
    std::string getCodecDescription() const;
    bool loadGvFile(const std::filesystem::path& path);
    void updatePlayback(double dt);
    bool updateGvFrame(int frameIndex);

    bool headlessMode_ = false;
    bool cliRequestedHelp_ = false;
    bool cliHasInput_ = false;
    bool isConverting_ = false;
    bool conversionDone_ = false;
    bool conversionFailed_ = false;
    bool createdIntermediate_ = false;

    InputKind inputKind_ = InputKind::None;
    std::filesystem::path inputPath_;
    std::vector<std::filesystem::path> inputQueue_;
    std::vector<std::string> inputQueueLabels_;
    std::string inputPathBuffer_;
    std::filesystem::path intermediateDir_;
    std::vector<std::filesystem::path> imagePaths_;

    EncodeSettings settings_;
    std::string statusMessage_;
    std::string lastError_;
    std::string imguiIniPath_;

    // Encoding state
    size_t currentIndex_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t bufferSize_ = 0;
    uint32_t squishFlag_ = 0;
    std::vector<uint8_t> gpuCompressBuffer_;
    std::vector<uint8_t> lz4CompressBuffer_;
    std::vector<Lz4Block> lz4Blocks_;
    std::unique_ptr<GpuVideoIO> io_;

    // GV playback
    std::filesystem::path gvPath_;
    std::unique_ptr<GpuVideoReader> gvReader_;
    Image gvImage_;
    std::vector<uint8_t> gvFrameBuffer_;
    std::vector<uint8_t> gvRgbaBuffer_;
    uint32_t gvWidth_ = 0;
    uint32_t gvHeight_ = 0;
    uint32_t gvFrameCount_ = 0;
    float gvFps_ = 0.0f;
    int gvFrameIndex_ = 0;
    bool gvPlaying_ = false;
    double gvFrameAccumulator_ = 0.0;
};
