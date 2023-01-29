#include <stdio.h>

#include <docopt.h>
#include "golpe.h"

#include "LMDBLoader.h"
#include "uiwrap.h"


static const char USAGE[] =
R"(
    Usage:
      viz <dbDir>
)";


std::vector<uint32_t> palette = {
    0x00aedb,
    0xa200ff,
    0xf47835,
    0xd41243,
    0x8ec127,
    0xff4e50,
    0xfc913a,
    0xf9d62e,
    0xeae374,
    0xe2f4c7,
    0xff00c1,
    0x9600ff,
    0x4900ff,
    0x00b8ff,
    0x70fff9,
};

static uint32_t getColour(uint64_t tableId, uint64_t type) {
    uint32_t col = palette[tableId % palette.size()];
    if (type) col ^= 0x666666;
    col |= 0xFF000000;
    return col;
}


std::string renderSize(uint64_t si) {
    if (si < 1024) return std::to_string(si) + " b";

    double s = si;
    char buf[128];
    char unit;

    do {
        s /= 1024;
        if (s < 1024) {
            unit = 'K';
            break;
        }

        s /= 1024;
        if (s < 1024) {
            unit = 'M';
            break;
        }

        s /= 1024;
        if (s < 1024) {
            unit = 'G';
            break;
        }

        s /= 1024;
        unit = 'T';
    } while(0);

    ::snprintf(buf, sizeof(buf), "%.2f %c", s, unit);
    return std::string(buf);
}


void cmd_viz(const std::vector<std::string> &subArgs) {
    std::map<std::string, docopt::value> args = docopt::docopt(USAGE, subArgs, true, "");

    std::string dbDir = args["<dbDir>"].asString();

    LMDBLoader loader(dbDir);
    auto dbDump = loader.crawl();
    if (!dbDump.tables.size() || !dbDump.offsets.size()) throw herr("empty db");


    struct TableState {
        std::string name;
        uint16_t tableId;
        bool keyVisible = true;
        bool valVisible = true;
        uint64_t keySpace = 0;
        uint64_t valSpace = 0;
    };

    std::vector<TableState> tables;

    for (const auto &t: dbDump.tables) {
        tables.emplace_back(t.name, t.tableId);
    }

    uint64_t mapEnd = loader.fileSize;
    uint64_t totalSpace = 0;

    for (const auto &o : dbDump.offsets) {
        if (o.type == 0) tables[o.tableId].keySpace += o.size;
        else if (o.type == 1) tables[o.tableId].valSpace += o.size;
        totalSpace += o.size;
    }


    uint64_t skipOffset = 0;
    uint64_t endOffset = 0;
    uint64_t bytesPerPixel = 0;


    startUi("LMDB-Viz", [&](auto *window){
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        GLuint texture;
        glGenTextures(1, &texture);

        auto renderDB = [&](uint64_t w, uint64_t h, uint64_t mag, float skip, float zoom){
            std::vector<uint32_t> pixelBuf(w * h, 0);

            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

            // Build bitmap
            {
                uint64_t wm = w / mag, hm = h / mag;
                std::vector<uint32_t> pixelBufMini(wm * hm, 0);

                skipOffset = mapEnd * skip;

                bytesPerPixel = mapEnd / (wm * hm);
                bytesPerPixel /= zoom;
                bytesPerPixel++;

                uint64_t lastPixelSet = 0;

                for (const auto &o : dbDump.offsets) {
                    if (o.start < skipOffset) continue;

                    uint64_t pixelIndex = (o.start - skipOffset) / bytesPerPixel;
                    uint64_t numPixels = 1 + (o.size / bytesPerPixel); // FIXME: the 1 is to show really small allocs

                    if (o.type == 0 && !tables[o.tableId].keyVisible) continue;
                    else if (o.type == 1 && !tables[o.tableId].valVisible) continue;

                    for (auto i = pixelIndex; i < pixelIndex + numPixels; i++) {
                        if (i < pixelBufMini.size()) pixelBufMini[i] = getColour(o.tableId, o.type);
                        lastPixelSet = i;
                    }

                    if (lastPixelSet >= pixelBufMini.size()) break;
                }

                endOffset = skipOffset + (bytesPerPixel * lastPixelSet);

                for (uint64_t i = lastPixelSet + 1; i < pixelBufMini.size(); i++) {
                    pixelBufMini[i] = 0x30FFFFFF;
                }

                // Magnify it into full window size

                for (uint64_t i = 0; i < pixelBuf.size(); i++) {
                    pixelBuf[i] = pixelBufMini[i/(mag*mag)];
                }
            }

            #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            #endif
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuf.data());
        };

        bool needRenderDB = true;
        uint64_t dbWindowW = 0;
        uint64_t dbWindowH = 0;
        uint64_t mag = 8;
        float skip = 0.0;
        float zoom = 1.0;

        while (!glfwWindowShouldClose(window)) {
            ImGui_ImplGlfw_WaitForEvent();
            glfwPollEvents();

            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& io = ImGui::GetIO();
            for (int c = 0; c < IM_ARRAYSIZE(io.KeysDown); c++) {
                if (!ImGui::IsKeyPressed(c)) continue;

                if (c == ',') mag /= 2;
                if (c == '.') mag *= 2;
                if (mag < 1) mag = 1;
                if (mag > 16) mag = 16;

                if (c == 265) skip -= 1 / zoom / 10;
                if (c == 264) skip += 1 / zoom / 10;
                if (skip < 0) skip = 0.0;
                if (skip > 1) skip = 1.0;

                if (c == '=') zoom *= 2;
                if (c == '-') zoom /= 2;
                if (zoom < .0001) zoom = .0001;

                needRenderDB = true;
            }

            ImGui::SetNextWindowSize(ImVec2(800, 800), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("DBs")) {
                if (ImGui::Button("Reset")) {
                    for (auto &table : tables) table.keyVisible = table.valVisible = true;
                    mag = 8;
                    skip = 0.0;
                    zoom = 1.0;
                    needRenderDB = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Hide all")) {
                    for (auto &table : tables) table.keyVisible = table.valVisible = false;
                    needRenderDB = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Show all")) {
                    for (auto &table : tables) table.keyVisible = table.valVisible = true;
                    needRenderDB = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Show keys")) {
                    for (auto &table : tables) table.keyVisible = true;
                    needRenderDB = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Show vals")) {
                    for (auto &table : tables) table.valVisible = true;
                    needRenderDB = true;
                }

                ImGui::Text("Total: %s", renderSize(totalSpace).c_str());
                ImGui::SameLine();
                ImGui::Text("File: %s", renderSize(loader.fileSize).c_str());
                ImGui::SameLine();
                ImGui::Text("Map: %s", renderSize(dbDump.mapSize).c_str());

                if (ImGui::BeginTable("dbs-table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("DB Name");
                    ImGui::TableSetupColumn("Show (key/val)");
                    ImGui::TableSetupColumn("Colour");
                    ImGui::TableSetupColumn("Key Size");
                    ImGui::TableSetupColumn("Val Size");
                    ImGui::TableHeadersRow();

                    for (auto &table : tables) {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", table.name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        if (ImGui::Checkbox((std::string("##show-key-") + std::to_string(table.tableId)).c_str(), &table.keyVisible)) needRenderDB = true;
                        ImGui::SameLine();
                        if (ImGui::Checkbox((std::string("##show-val-") + std::to_string(table.tableId)).c_str(), &table.valVisible)) needRenderDB = true;

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(getColour(table.tableId, 0)), "K");
                        ImGui::SameLine();
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(getColour(table.tableId, 1)), "V");

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%d (%.2f%%)", table.keySpace, 100.0*(double)table.keySpace/totalSpace);

                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%d (%.2f%%)", table.valSpace, 100.0*(double)table.valSpace/totalSpace);
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::End();

            {
                ImGui::SetNextWindowSize(ImVec2(2000, 1000), ImGuiCond_FirstUseEver);
                ImGui::Begin("DB Map");

                //ImVec2 wSizes = ImGui::GetWindowSize();
                ImVec2 wSizes = ImGui::GetWindowContentRegionMax();

                if (dbWindowW != wSizes[0] || dbWindowH != wSizes[1]) {
                    needRenderDB = true;
                    dbWindowW = wSizes[0];
                    dbWindowH = wSizes[1];
                }

                uint64_t vertPadding = ImGui::GetStyle().WindowPadding.y + ImGui::GetTextLineHeight() + 30;

                auto dbH = dbWindowH;
                if (dbH > vertPadding) dbH -= vertPadding;
                auto dbW = dbWindowW;

                if (needRenderDB) renderDB(dbW, dbH, mag, skip, zoom);
                needRenderDB = false;

                ImGui::Text("DB: %s   %s - %s   (row: %s, pixel: %s)",
                    dbDir.c_str(),
                    renderSize(skipOffset).c_str(),
                    renderSize(endOffset).c_str(),
                    renderSize(bytesPerPixel * dbW).c_str(),
                    renderSize(bytesPerPixel).c_str()
                );
                ImGui::Image((void*)(intptr_t)texture, ImVec2(dbW, dbH));
                ImGui::End();
            }

            // Rendering
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        }
    });
}
