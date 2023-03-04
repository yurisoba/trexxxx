#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

struct Instruction;

#define REG_COUNT 32
#define SCRATCH_COUNT 4

class Emulator
{
public:
    unsigned start_address = 0;
    std::vector<unsigned> memory;
    std::vector<Instruction *> i_cache;

    // state
    unsigned PC = 0;
    unsigned registers[REG_COUNT];
    unsigned scratch[SCRATCH_COUNT];

    // notify
    // TODO: better notification, require overhaul in UI architcture
    int notify_register = -1;  // written
    int notify_register2 = -1; // read

    void load_image(const char *filename);
    void step();

    void reset()
    {
        PC = start_address;
        for (unsigned int& i : scratch)
            i = 0;
        for (unsigned int& i : registers)
            i = 0;
        notify_register = -1;
        notify_register2 = -1;
    }

    unsigned get(unsigned addr)
    {
        // TODO: check word boundary
        return memory[(addr - start_address) / 4];
    }
};

struct Instruction
{
    const char *mnemonic = "???";
    unsigned word = -1;
    std::string arg;

    explicit Instruction(const char *m) : mnemonic(m) {}
    virtual void exec(Emulator& emu) = 0;
};

struct UnknownInstruction: Instruction
{
    UnknownInstruction() : Instruction("UNIMPLEMENTED/UNKNOWN") {}
    void exec(Emulator& emu) override {}
};

struct AddImmediateInstruction : Instruction
{
    bool shift = false;
    unsigned short rn = -1;
    unsigned short rd = -1;
    bool tempflag = false; // addi unimplemented, == is_mov()?

    explicit AddImmediateInstruction(unsigned ins) : Instruction("add") {
        // TODO: handle 32
        shift = (ins & (1 << 22)) != 0;
        rn = (ins >> 5) & 0b11111;
        rd = ins & 0b11111;
        unsigned imm12 = (ins >> 10) & 0b111111111111;

        char buf[64];
        if (!shift && imm12 == 0 && (rn == 0b11111 || rd == 0b11111)) {
            mnemonic = "mov";
            tempflag = true;
            if (rn == 0b11111) {
                sprintf(buf, "x%u, sp", rd);
            } else {
                sprintf(buf, "sp, x%u", rn);
            }
            arg = std::string(buf);
        } else {
            arg = "???";
        }
    }
    void exec(Emulator& emu) override {
        if (!tempflag)
            return;
        emu.registers[rd] += emu.registers[rn];
        emu.notify_register = rd;
        emu.notify_register2 = rn;
    }
};

struct LoadLiteralInstruction: Instruction
{
    unsigned offset = -1;   // offset from PC to load from
    unsigned short rt = -1; // register number

    explicit LoadLiteralInstruction(unsigned ins) : Instruction("ldr")
    {
        // TODO: differ 64 ldr and 32 ldr
        unsigned imm19 = (ins >> 5) & 0b1111111111111111111;
        mnemonic = "ldr";
        offset = imm19 * 4;
        rt = ins & 0b11111;
        // value = emu.get(PC + lli->offset);

        char buf[64];
        sprintf(buf, "x%u, <PC + 0x%x>", rt, offset);
        arg = std::string(buf);
    }

    void exec(Emulator& emu) override {
        emu.scratch[0] = emu.PC + offset;
        emu.scratch[1] = emu.get(emu.scratch[0]);
        emu.registers[rt] = emu.scratch[1];
        emu.notify_register = rt;
    }
};

void Emulator::load_image(const char *filename)
{
    int h = open(filename, O_RDONLY);

    ssize_t r;
    unsigned word;
    while ((r = read(h, &word, 4)) == 4) {
        memory.push_back(word);

        Instruction *instruction;
        if (((word >> 26) & 0b111) == 0b100) { // 100x
            instruction = new UnknownInstruction();
            instruction->mnemonic = "Data Processing - Immediate";
            if (((word >> 23) & 0b111) == 0b010) { // 010x
                instruction->mnemonic = "Add/subtract (immediate)";
                if ((word & (1 << 30)) == 0) { // x0x
                    instruction = new AddImmediateInstruction(word);
                }
            }
        }
        else if (((word & (1 << 27)) != 0) && ((word & (1 << 25)) == 0)) { // x1x0
            instruction = new UnknownInstruction();
            instruction->mnemonic = "Loads and Stores";
            if (((word & (0b11 << 28)) == (0b01 << 28)) && ((word & (1 << 24)) == 0))
                instruction = new LoadLiteralInstruction(word);
        }
        else {
            instruction = new UnknownInstruction();
        }
        instruction->word = word;
        i_cache.push_back(instruction);
    }
}

void Emulator::step()
{
    for (unsigned int& i : scratch)
        i = 0;
    notify_register = -1;
    notify_register2 = -1;

    unsigned ins_idx = (PC - start_address) / 4;
    i_cache[ins_idx]->exec(*this);
    PC += 4;
}

int main()
{
    // TODO: implement elf

    Emulator emu;
    emu.start_address = 0x80000;
    emu.load_image("kernel8.img");

    glfwSetErrorCallback([](int error, const char *description)
                         {
                             std::cerr << "GLFW ERROR " << error << ": " << description << std::endl;
                         });
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    const char *glsl_version = "#version 430 core";
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto *window = glfwCreateWindow(1280, 720, "TREXXXX", nullptr, nullptr);
    if (!window)
        throw std::runtime_error("Failed to create window");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.FontGlobalScale = 1.5f;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyReleased(ImGuiKey_Q))
            glfwSetWindowShouldClose(window, true);

        auto dockspace_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        if (ImGui::Begin("CTRL")) {
            if (ImGui::Button("RESET"))
                emu.reset();
            if (ImGui::Button("STEP"))
                emu.step();
            ImGui::End();
        }

        if (ImGui::Begin("D-ASM")) {
            if (ImGui::BeginTable("d-asm-t", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                unsigned PC = emu.start_address;
                for (const auto& ins : emu.i_cache) {
                    ImGui::TableNextColumn();
                    if (emu.PC == PC)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImColor(0, 120, 50).Value));
                    ImGui::Text("%x", PC);
                    ImGui::TableNextColumn(); ImGui::Text("%x", ins->word);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(ins->mnemonic);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(ins->arg.c_str());
                    PC += 4;
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        if (ImGui::Begin("STATE")) {
            if (ImGui::BeginTable("state-reg-t", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                // TODO: implement notify-state-change and check-state-change
                ImGui::TableNextColumn(); ImGui::TextUnformatted("sp");
                if (emu.notify_register == 31)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImColor(0, 0, 255).Value));
                if (emu.notify_register2 == 31)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImColor(150, 150, 0).Value));
                ImGui::TableNextColumn(); ImGui::Text("%08x", emu.registers[31]);
                for (int i = 0; i < REG_COUNT - 1; i++) {
                    ImGui::TableNextColumn();
                    if (emu.notify_register == i)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImColor(0, 0, 255).Value));
                    if (emu.notify_register2 == i)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImColor(150, 150, 0).Value));
                    ImGui::Text("x%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%08x", emu.registers[i]);
                }
                for (int i = 0; i < SCRATCH_COUNT; i++) {
                    ImGui::TableNextColumn();
                    ImGui::Text("scratch%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%08x", emu.scratch[i]);
                }

                ImGui::EndTable();
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
