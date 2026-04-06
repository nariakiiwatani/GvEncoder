#include "tcApp.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef __APPLE__
#include <sys/wait.h>
#endif

#include "tc/utils/tcJson.h"
#include "sokol/util/sokol_imgui.h"

namespace fs = std::filesystem;

static const char* kCodecNames[] = { "DXT1", "DXT3", "DXT5" };

static uint32_t codecToGpu(Codec codec) {
    switch (codec) {
        case Codec::DXT1: return GPU_COMPRESS_DXT1;
        case Codec::DXT3: return GPU_COMPRESS_DXT3;
        case Codec::DXT5: return GPU_COMPRESS_DXT5;
        default: return GPU_COMPRESS_DXT1;
    }
}

static int codecToSquish(Codec codec) {
    switch (codec) {
        case Codec::DXT1: return squish::kDxt1;
        case Codec::DXT3: return squish::kDxt3;
        case Codec::DXT5: return squish::kDxt5;
        default: return squish::kDxt1;
    }
}

static void printUsage() {
    std::cout
        << "GvEncoder usage:\n"
        << "  GvEncoder [options] <input>\n\n"
        << "Input:\n"
        << "  <input>              Image sequence folder or video file\n"
        << "  -i, --input <path>   Same as positional input\n\n"
        << "Options:\n"
        << "  -o, --output <path>  Output .gv file or directory\n"
        << "  -c, --codec <name>   dxt1 | dxt3 | dxt5 (default: dxt1)\n"
        << "  -r, --fps <value>    Override FPS\n"
        << "  -s, --size <WxH|scale>  Resize: pixel size (1920x1080) or scale (e.g. 0.5)\n"
        << "  --delete-source      Delete input after successful encode\n"
        << "  --gui                Force GUI even with args\n"
        << "  -h, --help           Show this help\n";
}

static void setupJapaneseFont() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts) return;
    // Merge Japanese glyphs into the default font (already built by imguiSetup)
    ImFontConfig config;
    config.MergeMode = true;
    const char* jpFontPaths[] = {
#ifdef __APPLE__
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
#endif
#ifdef _WIN32
        "C:\\Windows\\Fonts\\msgothic.ttc",
        "C:\\Windows\\Fonts\\meiryo.ttc",
#endif
    };
    for (const char* path : jpFontPaths) {
        if (io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesJapanese())) {
            break;
        }
    }
}

void tcApp::setup() {
    headlessMode_ = headless::isActive();
    if (!headlessMode_) {
        imguiSetup();
        setupJapaneseFont();
        imguiIniPath_ = getSettingsPath().replace_filename("imgui.ini").string();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = imguiIniPath_.c_str();
        ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }

    resetState();
    parseCommandLine();

    if (!headlessMode_) {
        loadGuiSettings();
    }

    if (inputKind_ == InputKind::None && !inputQueue_.empty()) {
        dequeueNextInput();
    }

    if (headlessMode_) {
        if (cliRequestedHelp_) {
            printUsage();
            requestExit();
            return;
        }
        if (!cliHasInput_) {
            printUsage();
            requestExit();
            return;
        }
        if (!startConversion()) {
            requestExit();
        }
    }
}

void tcApp::update() {
    if (isConverting_) {
        processBatch();
    }
    if (!headlessMode_) {
        updatePlayback(getDeltaTime());
    }
}

void tcApp::draw() {
    if (headlessMode_) {
        return;
    }

    clear(0.12f);
    imguiBegin();
    drawGui();
    imguiEnd();
}

void tcApp::cleanup() {
    if (!headlessMode_) {
        imguiShutdown();
    }
}

void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        requestExit();
    }
}
void tcApp::keyReleased(int key) {}

void tcApp::mousePressed(Vec2 pos, int button) {}
void tcApp::mouseReleased(Vec2 pos, int button) {}
void tcApp::mouseMoved(Vec2 pos) {}
void tcApp::mouseDragged(Vec2 pos, int button) {}
void tcApp::mouseScrolled(Vec2 delta) {}

void tcApp::windowResized(int width, int height) {}

void tcApp::filesDropped(const vector<string>& files) {
    if (files.empty()) {
        return;
    }
    for (const auto& file : files) {
        fs::path path = file;
        if (fs::exists(path) && fs::is_directory(path)) {
            auto imageDirs = collectImageDirectories(path);
            for (const auto& dir : imageDirs) {
                std::error_code ec;
                auto rel = fs::relative(dir, path, ec);
                std::string label = ec ? dir.filename().string() : rel.string();
                if (label.empty()) {
                    label = ".";
                }
                enqueueInputPath(dir, label);
            }
            continue;
        }
        string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".gv") {
            loadGvFile(path);
            continue;
        }
        enqueueInputPath(path, path.filename().string());
    }
}

void tcApp::exit() {
    if (!headlessMode_) {
        saveGuiSettings();
    }
}

void tcApp::parseCommandLine() {
    int argc = getArgCount();
    char** argv = getArgValues();
    if (argc <= 1) {
        return;
    }

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--gui") {
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            cliRequestedHelp_ = true;
            continue;
        }
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            if (enqueueInputPath(argv[++i])) {
                cliHasInput_ = true;
            }
            continue;
        }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            settings_.outputPath = argv[++i];
            continue;
        }
        if ((arg == "-c" || arg == "--codec") && i + 1 < argc) {
            string c = argv[++i];
            std::transform(c.begin(), c.end(), c.begin(), ::tolower);
            if (c == "dxt1") settings_.codec = Codec::DXT1;
            else if (c == "dxt3") settings_.codec = Codec::DXT3;
            else if (c == "dxt5") settings_.codec = Codec::DXT5;
            else {
                lastError_ = "Unknown codec: " + c;
                cliRequestedHelp_ = true;
            }
            continue;
        }
        if ((arg == "-r" || arg == "--fps") && i + 1 < argc) {
            settings_.fps = std::stof(argv[++i]);
            settings_.fpsSpecified = true;
            continue;
        }
        if ((arg == "-s" || arg == "--size") && i + 1 < argc) {
            string sizeStr = argv[++i];
            auto xPos = sizeStr.find('x');
            if (xPos != string::npos) {
                settings_.resizeWidth = std::stoi(sizeStr.substr(0, xPos));
                settings_.resizeHeight = std::stoi(sizeStr.substr(xPos + 1));
                settings_.resizeByScale = false;
                settings_.resizeEnabled = true;
            } else {
                try {
                    float scale = std::stof(sizeStr);
                    if (scale <= 0.0f || scale > 100.0f) {
                        lastError_ = "Scale must be in (0, 100]: " + sizeStr;
                        cliRequestedHelp_ = true;
                    } else {
                        settings_.resizeScale = scale;
                        settings_.resizeByScale = true;
                        settings_.resizeEnabled = true;
                    }
                } catch (...) {
                    lastError_ = "Invalid size format (use WxH or scale): " + sizeStr;
                    cliRequestedHelp_ = true;
                }
            }
            continue;
        }
        if (arg == "--delete-source") {
            settings_.deleteSource = true;
            continue;
        }
        if (!arg.empty() && arg[0] != '-' && !cliHasInput_) {
            if (enqueueInputPath(arg)) {
                cliHasInput_ = true;
            }
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            if (enqueueInputPath(arg)) {
                cliHasInput_ = true;
            }
            continue;
        }
    }
}

void tcApp::resetState() {
    isConverting_ = false;
    conversionDone_ = false;
    conversionFailed_ = false;
    createdIntermediate_ = false;
    inputKind_ = InputKind::None;
    inputPath_.clear();
    inputQueue_.clear();
    inputQueueLabels_.clear();
    inputPathBuffer_.clear();
    intermediateDir_.clear();
    imagePaths_.clear();
    statusMessage_.clear();
    lastError_.clear();
    currentIndex_ = 0;
    width_ = 0;
    height_ = 0;
    bufferSize_ = 0;
    squishFlag_ = 0;
    gpuCompressBuffer_.clear();
    lz4CompressBuffer_.clear();
    lz4Blocks_.clear();
    io_.reset();
}

void tcApp::clearInput() {
    inputKind_ = InputKind::None;
    inputPath_.clear();
    inputQueue_.clear();
    inputQueueLabels_.clear();
    inputPathBuffer_.clear();
    intermediateDir_.clear();
    imagePaths_.clear();
    createdIntermediate_ = false;
    statusMessage_ = "Input cleared";
}

bool tcApp::setInputPath(const fs::path& path) {
    if (!fs::exists(path)) {
        lastError_ = "Input path not found";
        return false;
    }

    inputPath_ = path;
    inputPathBuffer_ = path.string();
    if (fs::is_directory(path)) {
        inputKind_ = InputKind::ImageSequence;
        statusMessage_ = "Image sequence selected";
    } else {
        inputKind_ = InputKind::VideoFile;
        statusMessage_ = "Video file selected";
    }
    return true;
}

bool tcApp::enqueueInputPath(const fs::path& path, const std::string& displayLabel) {
    if (!fs::exists(path)) {
        lastError_ = "Input path not found";
        return false;
    }
    inputQueue_.push_back(path);
    if (displayLabel.empty()) {
        inputQueueLabels_.push_back(path.filename().string());
    } else {
        inputQueueLabels_.push_back(displayLabel);
    }
    return true;
}

bool tcApp::dequeueNextInput() {
    if (inputQueue_.empty()) {
        return false;
    }
    fs::path next = inputQueue_.front();
    inputQueue_.erase(inputQueue_.begin());
    if (!inputQueueLabels_.empty()) {
        inputQueueLabels_.erase(inputQueueLabels_.begin());
    }
    return setInputPath(next);
}

bool tcApp::startConversion() {
    lastError_.clear();
    conversionDone_ = false;
    conversionFailed_ = false;

    if (inputKind_ == InputKind::None && !inputQueue_.empty()) {
        dequeueNextInput();
    }

    if (inputKind_ == InputKind::None) {
        lastError_ = "Input not selected";
        return false;
    }

    if (settings_.skipIfUpToDate && isOutputUpToDate()) {
        statusMessage_ = "Skipped (up to date)";
        skipCurrentAndContinueQueue();
        return true;
    }

    if (!prepareInput()) {
        return false;
    }

    if (imagePaths_.empty()) {
        lastError_ = "No input frames found";
        return false;
    }

    if (settings_.resizeEnabled) {
        if (settings_.resizeByScale) {
            Pixels first;
            if (!first.load(imagePaths_.front())) {
                lastError_ = "Failed to load first image";
                return false;
            }
            int srcW = first.getWidth();
            int srcH = first.getHeight();
            settings_.resizeWidth = std::max(1, static_cast<int>(srcW * settings_.resizeScale));
            settings_.resizeHeight = std::max(1, static_cast<int>(srcH * settings_.resizeScale));
            width_ = static_cast<uint32_t>(settings_.resizeWidth);
            height_ = static_cast<uint32_t>(settings_.resizeHeight);
        } else {
            if (settings_.resizeWidth <= 0 || settings_.resizeHeight <= 0) {
                lastError_ = "Invalid resize dimensions";
                return false;
            }
            width_ = static_cast<uint32_t>(settings_.resizeWidth);
            height_ = static_cast<uint32_t>(settings_.resizeHeight);
        }
    } else {
        Pixels first;
        if (!first.load(imagePaths_.front())) {
            lastError_ = "Failed to load first image";
            return false;
        }
        width_ = static_cast<uint32_t>(first.getWidth());
        height_ = static_cast<uint32_t>(first.getHeight());
    }

    if (!settings_.fpsSpecified) {
        float detected = detectFpsFromMeta(inputKind_ == InputKind::VideoFile ? intermediateDir_ : inputPath_);
        if (detected > 0.0f) {
            settings_.fps = detected;
        }
    }

    auto outputPath = resolveOutputPath();
    if (outputPath.empty()) {
        lastError_ = "Output path could not be resolved";
        return false;
    }

    try {
        io_ = std::make_unique<GpuVideoIO>(outputPath.string().c_str(), "wb");
    } catch (const std::exception& e) {
        lastError_ = string("Failed to open output: ") + e.what();
        return false;
    }

    uint32_t flagQuality = squish::kColourIterativeClusterFit;
    squishFlag_ = flagQuality | codecToSquish(settings_.codec);
    bufferSize_ = squish::GetStorageRequirements(width_, height_, squishFlag_);

    // Header
    auto W = [&](auto& v) {
        if (io_->write(&v, sizeof(v)) != sizeof(v)) {
            lastError_ = "Failed to write header";
            return false;
        }
        return true;
    };

    uint32_t frameCount = static_cast<uint32_t>(imagePaths_.size());
    float fps = settings_.fps;
    uint32_t videoFmt = codecToGpu(settings_.codec);
    uint32_t frameBytes = bufferSize_;

    if (!W(width_)) return false;
    if (!W(height_)) return false;
    if (!W(frameCount)) return false;
    if (!W(fps)) return false;
    if (!W(videoFmt)) return false;
    if (!W(frameBytes)) return false;

    currentIndex_ = 0;
    lz4Blocks_.clear();
    isConverting_ = true;
    statusMessage_ = "Encoding started";
    return true;
}

void tcApp::processBatch() {
    if (!isConverting_) {
        return;
    }

    if (currentIndex_ >= imagePaths_.size()) {
        finishConversion(true);
        return;
    }

    const int kBatchCount = 16;
    size_t remaining = imagePaths_.size() - currentIndex_;
    size_t workCount = std::min(remaining, static_cast<size_t>(kBatchCount));

    int compressBound = LZ4_compressBound(static_cast<int>(bufferSize_));
    gpuCompressBuffer_.resize(workCount * bufferSize_);
    lz4CompressBuffer_.resize(workCount * compressBound);
    std::vector<int> lz4Sizes(workCount, 0);

    for (size_t i = 0; i < workCount; ++i) {
        const auto& srcPath = imagePaths_[currentIndex_ + i];
        Pixels img;
        if (!img.load(srcPath)) {
            lastError_ = "Failed to load image: " + srcPath.string();
            finishConversion(false);
            return;
        }

        if (settings_.resizeEnabled) {
            img = resizeNearest(img, static_cast<int>(width_), static_cast<int>(height_));
        } else if (img.getWidth() != static_cast<int>(width_) || img.getHeight() != static_cast<int>(height_)) {
            lastError_ = "Image size mismatch: " + srcPath.string();
            finishConversion(false);
            return;
        }

        uint8_t* dst = gpuCompressBuffer_.data() + i * bufferSize_;
        squish::CompressImage(img.getData(), width_, height_, dst, squishFlag_);
        lz4Sizes[i] = LZ4_compress_HC(
            reinterpret_cast<const char*>(dst),
            reinterpret_cast<char*>(lz4CompressBuffer_.data() + i * compressBound),
            static_cast<int>(bufferSize_),
            compressBound,
            LZ4HC_CLEVEL_DEFAULT
        );
        if (lz4Sizes[i] <= 0) {
            lastError_ = "LZ4 compression failed";
            finishConversion(false);
            return;
        }
    }

    uint64_t head = lz4Blocks_.empty()
        ? kRawMemoryAt
        : (lz4Blocks_.back().address + lz4Blocks_.back().size);

    for (size_t i = 0; i < workCount; ++i) {
        Lz4Block block;
        block.address = head;
        block.size = static_cast<uint64_t>(lz4Sizes[i]);
        head += block.size;
        lz4Blocks_.push_back(block);

        if (io_->write(lz4CompressBuffer_.data() + i * compressBound, lz4Sizes[i]) !=
            static_cast<size_t>(lz4Sizes[i])) {
            lastError_ = "Failed to write frame data";
            finishConversion(false);
            return;
        }
    }

    currentIndex_ += workCount;
    if (headlessMode_ && (currentIndex_ % 60 == 0 || currentIndex_ == imagePaths_.size())) {
        logNotice("GvEncoder") << "Progress: " << currentIndex_ << " / " << imagePaths_.size();
    }
}

void tcApp::finishConversion(bool success) {
    if (!isConverting_) {
        return;
    }

    if (success) {
        uint64_t size = lz4Blocks_.size() * sizeof(Lz4Block);
        if (io_ && io_->write(lz4Blocks_.data(), size) != size) {
            lastError_ = "Failed to write index table";
            success = false;
        }
    }

    io_.reset();
    isConverting_ = false;
    conversionDone_ = success;
    conversionFailed_ = !success;

    if (success) {
        statusMessage_ = "Encoding completed";
        auto outputPath = resolveOutputPath();
        if (!outputPath.empty()) {
            loadGvFile(outputPath);
            gvPlaying_ = true;
        }
    } else {
        statusMessage_ = "Encoding failed";
        auto outputPath = resolveOutputPath();
        if (!outputPath.empty()) {
            std::error_code ec;
            fs::remove(outputPath, ec);
        }
    }

    cleanupIntermediate();

    if (success && settings_.deleteSource) {
        std::error_code ec;
        if (inputKind_ == InputKind::ImageSequence) {
            fs::remove_all(inputPath_, ec);
        } else if (inputKind_ == InputKind::VideoFile) {
            fs::remove(inputPath_, ec);
        }
    }

    // Re-enqueue input so the user can press Run again after updating images
    if (settings_.keepInputAfterEncode && !inputPath_.empty()) {
        enqueueInputPath(inputPath_);
    }

    inputKind_ = InputKind::None;
    inputPath_.clear();
    imagePaths_.clear();

    if (!inputQueue_.empty()) {
        if (dequeueNextInput()) {
            if (!startConversion()) {
                if (headlessMode_) {
                    requestExit();
                }
            }
            return;
        }
    }

    if (headlessMode_) {
        requestExit();
    }
}

bool tcApp::prepareInput() {
    imagePaths_.clear();
    if (inputKind_ == InputKind::VideoFile) {
        if (!prepareVideoIntermediate()) {
            return false;
        }
        return buildImageList(intermediateDir_);
    }
    if (inputKind_ == InputKind::ImageSequence) {
        return buildImageList(inputPath_);
    }
    lastError_ = "Unsupported input type";
    return false;
}

bool tcApp::prepareVideoIntermediate() {
    fs::path input = inputPath_;
    fs::path dir = input.parent_path();
    string name = input.stem().string() + ".gvintermediate";
    intermediateDir_ = dir / name;

    if (fs::exists(intermediateDir_)) {
        std::error_code ec;
        fs::remove_all(intermediateDir_, ec);
    }
    fs::create_directories(intermediateDir_);
    createdIntermediate_ = true;

    int exitCode = 0;
    std::ostringstream metaCmd;
    metaCmd << "ffmpeg -i " << quotePath(input) << " 2>&1";
    string meta = runCommandCapture(metaCmd.str(), exitCode);
    if (exitCode != 0) {
        lastError_ = "ffmpeg metadata failed";
        return false;
    }

    fs::path metaPath = intermediateDir_ / "meta.txt";
    std::ofstream ofs(metaPath);
    ofs << meta;

    std::ostringstream extractCmd;
    extractCmd << "ffmpeg -y -i " << quotePath(input) << " -an ";
    if (settings_.resizeEnabled && !settings_.resizeByScale) {
        if (settings_.resizeWidth <= 0 || settings_.resizeHeight <= 0) {
            lastError_ = "Invalid resize dimensions";
            return false;
        }
        extractCmd << "-vf scale=" << settings_.resizeWidth << ":" << settings_.resizeHeight << " ";
    }
    if (settings_.fpsSpecified) {
        extractCmd << "-r " << settings_.fps << " ";
    }
    fs::path outPattern = intermediateDir_ / "%05d.png";
    extractCmd << "-vcodec png " << quotePath(outPattern);
    if (!runCommand(extractCmd.str(), exitCode) || exitCode != 0) {
        lastError_ = "ffmpeg extraction failed";
        return false;
    }
    return true;
}

bool tcApp::buildImageList(const fs::path& directory) {
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        lastError_ = "Input directory not found";
        return false;
    }

    imagePaths_.clear();
    for (auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto path = entry.path();
        auto name = path.filename().string();
        if (!name.empty() && name[0] == '.') {
            continue;
        }
        if (isImageExtension(path)) {
            imagePaths_.push_back(path);
        }
    }

    std::sort(imagePaths_.begin(), imagePaths_.end());
    if (imagePaths_.empty()) {
        lastError_ = "No images found in directory";
        return false;
    }
    return true;
}

bool tcApp::isImageExtension(const fs::path& path) const {
    string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" || ext == ".tiff";
}

std::vector<fs::path> tcApp::collectImageDirectories(const fs::path& root) const {
    std::vector<fs::path> results;
    if (!fs::exists(root) || !fs::is_directory(root)) {
        return results;
    }

    std::set<fs::path> uniqueDirs;
    for (auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto path = entry.path();
        auto name = path.filename().string();
        if (!name.empty() && name[0] == '.') {
            continue;
        }
        if (isImageExtension(path)) {
            uniqueDirs.insert(path.parent_path());
        }
    }

    results.assign(uniqueDirs.begin(), uniqueDirs.end());
    std::sort(results.begin(), results.end());
    return results;
}

fs::path tcApp::resolveOutputPath() const {
    fs::path output = settings_.outputPath;
    fs::path baseName;
    if (inputKind_ == InputKind::ImageSequence) {
        baseName = inputPath_.filename();
    } else if (inputKind_ == InputKind::VideoFile) {
        baseName = inputPath_.stem();
    }

    if (output.empty()) {
        output = inputPath_.parent_path() / (baseName.string() + ".gv");
    } else {
        if (fs::is_directory(output)) {
            output = output / (baseName.string() + ".gv");
        } else if (output.extension() != ".gv") {
            output.replace_extension(".gv");
        }
    }
    return output;
}

std::optional<fs::file_time_type> tcApp::getNewestSourceMtime() const {
    if (inputPath_.empty()) {
        return std::nullopt;
    }
    if (inputKind_ == InputKind::VideoFile) {
        if (!fs::exists(inputPath_) || !fs::is_regular_file(inputPath_)) {
            return std::nullopt;
        }
        return fs::last_write_time(inputPath_);
    }
    if (inputKind_ == InputKind::ImageSequence) {
        if (!fs::exists(inputPath_) || !fs::is_directory(inputPath_)) {
            return std::nullopt;
        }
        std::optional<fs::file_time_type> newest;
        for (const auto& entry : fs::directory_iterator(inputPath_)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto path = entry.path();
            if (!isImageExtension(path)) {
                continue;
            }
            auto t = fs::last_write_time(path);
            if (!newest || t > *newest) {
                newest = t;
            }
        }
        return newest;
    }
    return std::nullopt;
}

bool tcApp::isOutputUpToDate() const {
    fs::path out = resolveOutputPath();
    if (out.empty() || !fs::exists(out) || !fs::is_regular_file(out)) {
        return false;
    }
    auto gvTime = fs::last_write_time(out);
    auto srcOpt = getNewestSourceMtime();
    if (!srcOpt) {
        return false;
    }
    return gvTime >= *srcOpt;
}

void tcApp::skipCurrentAndContinueQueue() {
    if (settings_.keepInputAfterEncode && !inputPath_.empty()) {
        enqueueInputPath(inputPath_);
    }
    inputKind_ = InputKind::None;
    inputPath_.clear();
    imagePaths_.clear();
    if (!inputQueue_.empty()) {
        if (dequeueNextInput()) {
            startConversion();
        }
        return;
    }
    if (headlessMode_) {
        requestExit();
    }
}

float tcApp::detectFpsFromMeta(const fs::path& directory) const {
    fs::path metaPath = directory / "meta.txt";
    if (!fs::exists(metaPath)) {
        return 30.0f;
    }
    std::ifstream ifs(metaPath);
    std::string meta((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    std::regex regex("Video:.*, ?([\\d\\.]+)\\s?fps");
    std::smatch match;
    if (std::regex_search(meta, match, regex) && match.size() == 2) {
        return std::stof(match[1].str());
    }
    return 30.0f;
}

std::string tcApp::runCommandCapture(const std::string& cmd, int& exitCode) const {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return result;
    }
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    int status = pclose(pipe);
#ifdef __APPLE__
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    } else {
        exitCode = status;
    }
#else
    exitCode = status;
#endif
    return result;
}

bool tcApp::runCommand(const std::string& cmd, int& exitCode) const {
    int status = std::system(cmd.c_str());
#ifdef __APPLE__
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    } else {
        exitCode = status;
    }
#else
    exitCode = status;
#endif
    return exitCode == 0;
}

std::string tcApp::quotePath(const fs::path& path) const {
    std::string s = path.string();
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

Pixels tcApp::resizeNearest(const Pixels& src, int width, int height) const {
    Pixels dst;
    dst.allocate(width, height, src.getChannels());
    const int srcW = src.getWidth();
    const int srcH = src.getHeight();
    const int channels = src.getChannels();
    const unsigned char* srcData = src.getData();
    unsigned char* dstData = dst.getData();

    for (int y = 0; y < height; ++y) {
        int sy = static_cast<int>((static_cast<float>(y) / height) * srcH);
        sy = std::min(std::max(sy, 0), srcH - 1);
        for (int x = 0; x < width; ++x) {
            int sx = static_cast<int>((static_cast<float>(x) / width) * srcW);
            sx = std::min(std::max(sx, 0), srcW - 1);
            int srcIndex = (sy * srcW + sx) * channels;
            int dstIndex = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c) {
                dstData[dstIndex + c] = srcData[srcIndex + c];
            }
        }
    }
    return dst;
}

void tcApp::cleanupIntermediate() {
    if (createdIntermediate_ && !intermediateDir_.empty()) {
        std::error_code ec;
        fs::remove_all(intermediateDir_, ec);
        createdIntermediate_ = false;
    }
}

void tcApp::drawGui() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(getWindowWidth() * 0.55f, getWindowHeight() - 40), ImGuiCond_Once);
    ImGui::Begin("GvEncoder");

    ImGui::Text("Inputs");
    ImGui::Separator();
    ImGui::Text("Current: %s", inputPath_.empty() ? "(none)" : inputPath_.string().c_str());
    if (!inputQueue_.empty()) {
        ImGui::Text("Queue:");
        for (size_t i = 0; i < inputQueue_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::string label = inputQueueLabels_.size() > i
                ? inputQueueLabels_[i]
                : inputQueue_[i].string();
            ImGui::Text("  %zu: %s", i + 1, label.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                inputQueue_.erase(inputQueue_.begin() + static_cast<long>(i));
                if (inputQueueLabels_.size() > i) {
                    inputQueueLabels_.erase(inputQueueLabels_.begin() + static_cast<long>(i));
                }
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    } else {
        ImGui::Text("Queue: (empty)");
    }

    if (!isConverting_) {
        if (ImGui::Button("Select Input", ImVec2(160, 28))) {
            auto result = loadDialog("Select input (file or folder)");
            if (result.success) {
                fs::path selected = result.filePath;
                std::string label = selected.filename().string();
                if (fs::exists(selected) && fs::is_directory(selected)) {
                    label = ".";
                }
                enqueueInputPath(selected, label);
            }
        }
        if (ImGui::Button("Clear Input", ImVec2(140, 28))) {
            clearInput();
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tip: Drag & drop files or folders.");
    }

    ImGui::Separator();

    int codecIndex = static_cast<int>(settings_.codec);
    ImGui::Combo("Codec", &codecIndex, kCodecNames, 3);
    settings_.codec = static_cast<Codec>(codecIndex);
    ImGui::TextWrapped("%s", getCodecDescription().c_str());

    ImGui::Checkbox("Override FPS", &settings_.fpsSpecified);
    ImGui::InputFloat("FPS", &settings_.fps);
    settings_.fps = std::max(1.0f, settings_.fps);

    ImGui::Checkbox("Resize", &settings_.resizeEnabled);
    if (settings_.resizeEnabled) {
        ImGui::Checkbox("By scale", &settings_.resizeByScale);
        if (settings_.resizeByScale) {
            ImGui::InputFloat("Scale", &settings_.resizeScale);
            settings_.resizeScale = std::max(0.01f, std::min(100.0f, settings_.resizeScale));
        } else {
            ImGui::InputInt("Width", &settings_.resizeWidth);
            ImGui::InputInt("Height", &settings_.resizeHeight);
        }
    }

    ImGui::Separator();
    auto resolvedOutput = settings_.outputPath.empty() ? "(auto)" : settings_.outputPath.string();
    ImGui::Text("Output: %s", resolvedOutput.c_str());
    if (!isConverting_) {
        if (ImGui::Button("Select Output", ImVec2(160, 28))) {
            string defaultName = inputPath_.empty() ? "output.gv" : resolveOutputPath().filename().string();
            auto result = saveDialog("Save .gv", "", "", defaultName);
            if (result.success) {
                settings_.outputPath = result.filePath;
            }
        }
    }

    ImGui::Checkbox("Delete Source After Encode", &settings_.deleteSource);
    ImGui::Checkbox("Skip if image is not updated", &settings_.skipIfUpToDate);
    ImGui::Checkbox("Keep input after encode", &settings_.keepInputAfterEncode);

    ImGui::Separator();
    if (!isConverting_) {
        // Run は Current があるか、Queue に何かあるときに押せる（Queue があれば押下時に dequeue して開始）
        if (inputKind_ != InputKind::None || !inputQueue_.empty()) {
            if (ImGui::Button("Run", ImVec2(160, 32))) {
                startConversion();
            }
        } else {
            ImGui::Text("Please select input.");
        }
    } else {
        ImGui::Text("Encoding... %zu / %zu", currentIndex_, imagePaths_.size());
    }

    if (!statusMessage_.empty()) {
        ImGui::Text("Status: %s", statusMessage_.c_str());
    }
    if (!lastError_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", lastError_.c_str());
    }

    ImGui::Separator();
    ImGui::Text("Command Line");
    std::string cmdLine = buildCommandLine();
    // 長いコマンドでも途切れないよう動的バッファを使用（ImGui は null 終端を要求）
    static std::vector<char> cmdBuffer;
    cmdBuffer.assign(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');
    float cmdLineHeight = std::min(120.0f, 20.0f + std::count(cmdLine.begin(), cmdLine.end(), '\n') * 18.0f);
    ImGui::InputTextMultiline("##cmdline", cmdBuffer.data(), cmdBuffer.size(), ImVec2(-1, cmdLineHeight),
        ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Copy to Clipboard", ImVec2(180, 26))) {
        setClipboardString(cmdLine);
    }

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(getWindowWidth() * 0.6f, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(getWindowWidth() * 0.38f, getWindowHeight() - 40), ImGuiCond_Once);
    ImGui::Begin("GV Player");

    if (ImGui::Button("Open GV File", ImVec2(160, 26))) {
        auto result = loadDialog("Select .gv file");
        if (result.success) {
            loadGvFile(result.filePath);
        }
    }
    ImGui::SameLine();
    ImGui::Text("%s", gvPath_.empty() ? "(none)" : gvPath_.filename().string().c_str());

    if (gvReader_) {
        ImGui::Text("Size: %u x %u  Frames: %u  FPS: %.2f",
            gvWidth_, gvHeight_, gvFrameCount_, gvFps_);

        if (gvFrameCount_ > 0) {
            if (ImGui::Button(gvPlaying_ ? "Pause" : "Play", ImVec2(100, 26))) {
                gvPlaying_ = !gvPlaying_;
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop", ImVec2(80, 26))) {
                gvPlaying_ = false;
                gvFrameIndex_ = 0;
                updateGvFrame(gvFrameIndex_);
            }
            ImGui::SameLine();
            if (ImGui::Button("Step", ImVec2(80, 26))) {
                gvPlaying_ = false;
                gvFrameIndex_ = std::min(gvFrameIndex_ + 1, static_cast<int>(gvFrameCount_) - 1);
                updateGvFrame(gvFrameIndex_);
            }

            int frameIndex = gvFrameIndex_;
            if (ImGui::SliderInt("Frame", &frameIndex, 0, static_cast<int>(gvFrameCount_) - 1)) {
                gvPlaying_ = false;
                updateGvFrame(frameIndex);
            }
        }

        if (gvImage_.isAllocated()) {
            const Texture& tex = gvImage_.getTexture();
            if (tex.isAllocated()) {
                float availWidth = ImGui::GetContentRegionAvail().x;
                float previewHeight = 240.0f;
                float aspect = static_cast<float>(gvHeight_) / gvWidth_;
                float previewW = std::min(availWidth, previewHeight / aspect);
                float previewH = previewW * aspect;
                if (previewH > previewHeight) {
                    previewH = previewHeight;
                    previewW = previewH / aspect;
                }
                ImTextureID texId = simgui_imtextureid(tex.getView());
                ImGui::Image(texId, ImVec2(previewW, previewH));
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1), "No GV loaded");
    }

    ImGui::End();
}

void tcApp::loadGuiSettings() {
    fs::path path = getSettingsPath();
    if (!fs::exists(path)) {
        return;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return;
    }

    Json j = Json::parse(ifs, nullptr, false);
    if (j.is_discarded()) {
        return;
    }

    string codec = j.value("codec", "dxt1");
    std::transform(codec.begin(), codec.end(), codec.begin(), ::tolower);
    if (codec == "dxt1") settings_.codec = Codec::DXT1;
    else if (codec == "dxt3") settings_.codec = Codec::DXT3;
    else if (codec == "dxt5") settings_.codec = Codec::DXT5;

    settings_.fps = j.value("fps", settings_.fps);
    settings_.fpsSpecified = j.value("fpsSpecified", settings_.fpsSpecified);
    settings_.resizeEnabled = j.value("resizeEnabled", settings_.resizeEnabled);
    settings_.resizeByScale = j.value("resizeByScale", settings_.resizeByScale);
    settings_.resizeScale = j.value("resizeScale", settings_.resizeScale);
    settings_.resizeWidth = j.value("resizeWidth", settings_.resizeWidth);
    settings_.resizeHeight = j.value("resizeHeight", settings_.resizeHeight);
    settings_.deleteSource = j.value("deleteSource", settings_.deleteSource);
    settings_.skipIfUpToDate = j.value("skipIfUpToDate", settings_.skipIfUpToDate);
    settings_.keepInputAfterEncode = j.value("keepInputAfterEncode", settings_.keepInputAfterEncode);

    string outputPath = j.value("outputPath", "");
    if (!outputPath.empty()) {
        settings_.outputPath = outputPath;
    }
}

void tcApp::saveGuiSettings() const {
    fs::path path = getSettingsPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    Json j;
    switch (settings_.codec) {
        case Codec::DXT1: j["codec"] = "dxt1"; break;
        case Codec::DXT3: j["codec"] = "dxt3"; break;
        case Codec::DXT5: j["codec"] = "dxt5"; break;
    }
    j["fps"] = settings_.fps;
    j["fpsSpecified"] = settings_.fpsSpecified;
    j["resizeEnabled"] = settings_.resizeEnabled;
    j["resizeByScale"] = settings_.resizeByScale;
    j["resizeScale"] = settings_.resizeScale;
    j["resizeWidth"] = settings_.resizeWidth;
    j["resizeHeight"] = settings_.resizeHeight;
    j["deleteSource"] = settings_.deleteSource;
    j["skipIfUpToDate"] = settings_.skipIfUpToDate;
    j["keepInputAfterEncode"] = settings_.keepInputAfterEncode;
    j["outputPath"] = settings_.outputPath.empty() ? "" : settings_.outputPath.string();

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return;
    }
    ofs << j.dump(2);
}

fs::path tcApp::getSettingsPath() const {
    fs::path path = fs::path(getDataPath("gv_encoder_settings.json"));
    return path;
}

std::string tcApp::buildCommandLine() const {
    std::ostringstream cmd;
    cmd << "GvEncoder";

    cmd << " -c " << (settings_.codec == Codec::DXT1 ? "dxt1" :
                       settings_.codec == Codec::DXT3 ? "dxt3" : "dxt5");

    if (settings_.fpsSpecified) {
        cmd << " -r " << settings_.fps;
    }
    if (settings_.resizeEnabled) {
        if (settings_.resizeByScale) {
            cmd << " -s " << settings_.resizeScale;
        } else {
            cmd << " -s " << settings_.resizeWidth << "x" << settings_.resizeHeight;
        }
    }
    if (!settings_.outputPath.empty()) {
        cmd << " -o " << quotePath(settings_.outputPath);
    }
    if (settings_.deleteSource) {
        cmd << " --delete-source";
    }

    if (inputKind_ != InputKind::None && !inputPath_.empty()) {
        cmd << " " << quotePath(inputPath_);
        for (const auto& path : inputQueue_) {
            cmd << " " << quotePath(path);
        }
    } else if (!inputQueue_.empty()) {
        for (const auto& path : inputQueue_) {
            cmd << " " << quotePath(path);
        }
    } else {
        cmd << " <input>";
    }
    return cmd.str();
}

std::string tcApp::getCodecDescription() const {
    switch (settings_.codec) {
        case Codec::DXT1:
            return "RGB focused. No alpha or 1-bit alpha. Smallest size.";
        case Codec::DXT3:
            return "4-bit explicit alpha. Sharp edges and UI friendly.";
        case Codec::DXT5:
            return "Interpolated alpha. Best for smooth transparency.";
        default:
            return "";
    }
}

bool tcApp::loadGvFile(const fs::path& path) {
    string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!fs::exists(path) || ext != ".gv") {
        lastError_ = "Invalid GV file";
        return false;
    }

    try {
        gvReader_ = std::make_unique<GpuVideoReader>(path.string().c_str(), false);
    } catch (const std::exception& e) {
        lastError_ = string("Failed to open GV: ") + e.what();
        return false;
    }

    gvPath_ = path;
    gvWidth_ = gvReader_->getWidth();
    gvHeight_ = gvReader_->getHeight();
    gvFrameCount_ = gvReader_->getFrameCount();
    gvFps_ = gvReader_->getFramePerSecond();
    if (gvFps_ <= 0.0f) {
        gvFps_ = 30.0f;
    }

    gvFrameBuffer_.resize(gvReader_->getFrameBytes());
    gvRgbaBuffer_.resize(static_cast<size_t>(gvWidth_) * gvHeight_ * 4);
    gvImage_.allocate(static_cast<int>(gvWidth_), static_cast<int>(gvHeight_), 4);
    gvFrameIndex_ = 0;
    gvFrameAccumulator_ = 0.0;
    gvPlaying_ = false;

    return updateGvFrame(gvFrameIndex_);
}

void tcApp::updatePlayback(double dt) {
    if (!gvReader_ || !gvPlaying_ || gvFrameCount_ == 0) {
        return;
    }

    double frameDuration = 1.0 / std::max(1.0f, gvFps_);
    gvFrameAccumulator_ += dt;
    while (gvFrameAccumulator_ >= frameDuration) {
        gvFrameAccumulator_ -= frameDuration;
        gvFrameIndex_ = (gvFrameIndex_ + 1) % static_cast<int>(gvFrameCount_);
        updateGvFrame(gvFrameIndex_);
    }
}

bool tcApp::updateGvFrame(int frameIndex) {
    if (!gvReader_ || gvFrameCount_ == 0) {
        return false;
    }

    frameIndex = std::max(0, std::min(frameIndex, static_cast<int>(gvFrameCount_) - 1));
    gvReader_->read(gvFrameBuffer_.data(), frameIndex);

    int squishFlag = 0;
    switch (gvReader_->getFormat()) {
        case GPU_COMPRESS_DXT1: squishFlag = squish::kDxt1; break;
        case GPU_COMPRESS_DXT3: squishFlag = squish::kDxt3; break;
        case GPU_COMPRESS_DXT5: squishFlag = squish::kDxt5; break;
        default:
            lastError_ = "Unsupported GV format";
            return false;
    }

    squish::DecompressImage(
        gvRgbaBuffer_.data(),
        gvWidth_,
        gvHeight_,
        gvFrameBuffer_.data(),
        squishFlag
    );

    gvImage_.getPixels().setFromPixels(gvRgbaBuffer_.data(), gvWidth_, gvHeight_, 4);
    gvImage_.setDirty();
    gvImage_.update();
    gvFrameIndex_ = frameIndex;
    return true;
}
