// Process-lifetime arena of hook slots and their executable gates.
//
// Slots, generated code, unwind tables and MinHook's retained trampolines have process
// lifetime and are never reassigned to a different hook generation, so a late native entrant
// can always run the gate it captured. Chunked allocation keeps the hot path allocation-free
// and generated code W^X (written while RW, then sealed RX before publication).

#include <windows.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "native_hook_detail.h"

namespace ark::core {
namespace {

constexpr size_t kSlotsPerChunk = 256;
constexpr size_t kGateStride = 128;
constexpr size_t kGatesPerSlot = 4;
constexpr size_t kGatesPerChunk = kSlotsPerChunk * kGatesPerSlot;
constexpr size_t kGateCodeSize = kGateStride * kGatesPerChunk;
constexpr size_t kGateSize = 45;
constexpr size_t kGateV2Size = 108;
constexpr size_t kUnwindInfoSize = 8;

// Byte offsets of the 64-bit immediates patched into each gate.
constexpr size_t kGateSlotImmediate = 20;
constexpr size_t kGateGatewayImmediate = 30;
constexpr size_t kGateV2SlotImmediate = 83;
constexpr size_t kGateV2GatewayImmediate = 93;

// A gate turns gateway(slot, a, b, c, d) into a native four-argument function. On entry the
// caller's RSP is 8 mod 16: reserve 32 bytes of shadow space plus the fifth argument slot,
// shift RCX/RDX/R8/R9 one register to the right and load the slot into RCX.
constexpr std::array<unsigned char, kGateSize> kGateTemplate = {
    0x48, 0x83, 0xec, 0x28,                      // sub rsp,40
    0x4c, 0x89, 0x4c, 0x24, 0x20,                // mov [rsp+32],r9
    0x4d, 0x89, 0xc1,                            // mov r9,r8
    0x49, 0x89, 0xd0,                            // mov r8,rdx
    0x48, 0x89, 0xca,                            // mov rdx,rcx
    0x48, 0xb9, 0,    0,    0,    0, 0, 0, 0, 0, // mov rcx,slot
    0x48, 0xb8, 0,    0,    0,    0, 0, 0, 0, 0, // mov rax,gateway
    0xff, 0xd0,                                  // call rax
    0x48, 0x83, 0xc4, 0x28,                      // add rsp,40
    0xc3,                                        // ret
};

// v2 copies the four register arguments and four stack arguments into a
// NativeHookCall on this frame, then passes that call and the slot to the v2
// gateway. The callee owns only integer/pointer arguments.
constexpr std::array<unsigned char, kGateV2Size> kGateV2Template = {
    0x48, 0x83, 0xec, 0x68,                      // sub rsp,104
    0x48, 0x89, 0x4c, 0x24, 0x20,                // mov [rsp+32],rcx
    0x48, 0x89, 0x54, 0x24, 0x28,                // mov [rsp+40],rdx
    0x4c, 0x89, 0x44, 0x24, 0x30,                // mov [rsp+48],r8
    0x4c, 0x89, 0x4c, 0x24, 0x38,                // mov [rsp+56],r9
    0x48, 0x8b, 0x84, 0x24, 0x90, 0, 0, 0,       // mov rax,[rsp+144]
    0x48, 0x89, 0x44, 0x24, 0x40,                // mov [rsp+64],rax
    0x48, 0x8b, 0x84, 0x24, 0x98, 0, 0, 0,       // mov rax,[rsp+152]
    0x48, 0x89, 0x44, 0x24, 0x48,                // mov [rsp+72],rax
    0x48, 0x8b, 0x84, 0x24, 0xa0, 0, 0, 0,       // mov rax,[rsp+160]
    0x48, 0x89, 0x44, 0x24, 0x50,                // mov [rsp+80],rax
    0x48, 0x8b, 0x84, 0x24, 0xa8, 0, 0, 0,       // mov rax,[rsp+168]
    0x48, 0x89, 0x44, 0x24, 0x58,                // mov [rsp+88],rax
    0x48, 0x8d, 0x4c, 0x24, 0x20,                // lea rcx,[rsp+32]
    0x48, 0xba, 0, 0, 0, 0, 0, 0, 0, 0,          // mov rdx,slot
    0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,          // mov rax,gateway
    0xff, 0xd0,                                  // call rax
    0x48, 0x83, 0xc4, 0x68,                      // add rsp,104
    0xc3,                                        // ret
};

static_assert(kGateSize <= kGateStride, "v1 gate overruns its arena stride");
static_assert(kGateV2Size <= kGateStride, "v2 gate overruns its arena stride");
static_assert(kGateV2Template.size() == kGateV2Size,
              "v2 gate length must match its unwind range");
// The two immediate patches must land on the imm64 operands of `mov rdx,slot`
// and `mov rax,gateway`.
static_assert(kGateV2Template[kGateV2SlotImmediate - 2] == 0x48 &&
                  kGateV2Template[kGateV2SlotImmediate - 1] == 0xba,
              "v2 slot immediate offset is misplaced");
static_assert(kGateV2Template[kGateV2GatewayImmediate - 2] == 0x48 &&
                  kGateV2Template[kGateV2GatewayImmediate - 1] == 0xb8,
              "v2 gateway immediate offset is misplaced");
static_assert(kGateV2SlotImmediate + sizeof(void*) <= kGateV2Size);
static_assert(kGateV2GatewayImmediate + sizeof(void*) <= kGateV2Size);

// UNWIND_INFO version 1 shared by every gate: 4-byte prologue, one UWOP_ALLOC_SMALL at
// offset 4 with OpInfo=4 (4*8+8 = 40 bytes). The single unwind code is padded to two slots.
constexpr std::array<unsigned char, kUnwindInfoSize> kGateUnwindInfo = {1, 4, 1, 0, 4, 0x42, 0, 0};
constexpr std::array<unsigned char, kUnwindInfoSize> kGateV2UnwindInfo =
    {1, 4, 1, 0, 4, 0xC2, 0, 0};

void emit_gate(unsigned char* code, NativeHookSlot* slot, void* gateway) {
    std::memcpy(code, kGateTemplate.data(), kGateTemplate.size());
    std::memcpy(code + kGateSlotImmediate, &slot, sizeof slot);
    std::memcpy(code + kGateGatewayImmediate, &gateway, sizeof gateway);
}

void emit_gate_v2(unsigned char* code, NativeHookSlot* slot, void* gateway) {
    std::memcpy(code, kGateV2Template.data(), kGateV2Template.size());
    std::memcpy(code + kGateV2SlotImmediate, &slot, sizeof slot);
    std::memcpy(code + kGateV2GatewayImmediate, &gateway, sizeof gateway);
}

std::string last_error_message(const char* operation) {
    const auto code = GetLastError();
    return std::string("Native gate ") + operation + " failed: " + std::to_string(code);
}

struct Chunk {
    std::array<NativeHookSlot, kSlotsPerChunk> slots;
    std::array<RUNTIME_FUNCTION, kGatesPerChunk> functions{};
    // kGateCodeSize bytes of gates followed by the two shared UNWIND_INFO records.
    unsigned char* code = nullptr;
    size_t used = 0;
    bool registered = false;

    Chunk() = default;
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    ~Chunk() {
        // Published chunks live in the intentionally immortal arena. Destruction happens only
        // when unpublished construction fails.
        if (registered) {
            RtlDeleteFunctionTable(functions.data());
        }
        if (code) {
            VirtualFree(code, 0, MEM_RELEASE);
        }
    }
};

void emit_chunk_gates(Chunk& chunk) {
    std::memset(chunk.code, 0xcc, kGateCodeSize);
    std::memcpy(chunk.code + kGateCodeSize, kGateUnwindInfo.data(),
                kGateUnwindInfo.size());
    std::memcpy(chunk.code + kGateCodeSize + kUnwindInfoSize,
                kGateV2UnwindInfo.data(), kGateV2UnwindInfo.size());
    for (size_t i = 0; i != kSlotsPerChunk; ++i) {
        auto& slot = chunk.slots[i];
        slot.detour_gate = chunk.code + (i * kGatesPerSlot) * kGateStride;
        slot.original_gate =
            chunk.code + (i * kGatesPerSlot + 1) * kGateStride;
        slot.v2_detour_gate =
            chunk.code + (i * kGatesPerSlot + 2) * kGateStride;
        slot.v2_original_gate =
            chunk.code + (i * kGatesPerSlot + 3) * kGateStride;
        emit_gate(static_cast<unsigned char*>(slot.detour_gate), &slot,
                  reinterpret_cast<void*>(detour_gateway));
        emit_gate(static_cast<unsigned char*>(slot.original_gate), &slot,
                  reinterpret_cast<void*>(original_gateway));
        emit_gate_v2(static_cast<unsigned char*>(slot.v2_detour_gate), &slot,
                     reinterpret_cast<void*>(v2_detour_gateway));
        emit_gate_v2(static_cast<unsigned char*>(slot.v2_original_gate), &slot,
                     reinterpret_cast<void*>(v2_original_gateway));
    }
    for (size_t i = 0; i != kGatesPerChunk; ++i) {
        auto& function = chunk.functions[i];
        function.BeginAddress = static_cast<DWORD>(i * kGateStride);
        const bool v2 = (i % kGatesPerSlot) >= 2;
        function.EndAddress = static_cast<DWORD>(
            i * kGateStride + (v2 ? kGateV2Size : kGateSize));
        function.UnwindData = static_cast<DWORD>(
            kGateCodeSize + (v2 ? kUnwindInfoSize : 0));
    }
}

std::unique_ptr<Chunk> make_chunk(std::string& error) {
    auto chunk = std::make_unique<Chunk>();
    chunk->code = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, kGateCodeSize + kUnwindInfoSize * 2, MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE));
    if (!chunk->code) {
        error = last_error_message("VirtualAlloc");
        return {};
    }
    emit_chunk_gates(*chunk);
    DWORD previous_protection = 0;
    if (!VirtualProtect(chunk->code, kGateCodeSize + kUnwindInfoSize * 2,
                        PAGE_EXECUTE_READ, &previous_protection)) {
        error = last_error_message("VirtualProtect");
        return {};
    }
    if (!FlushInstructionCache(GetCurrentProcess(), chunk->code, kGateCodeSize)) {
        error = last_error_message("FlushInstructionCache");
        return {};
    }
    if (!RtlAddFunctionTable(chunk->functions.data(), static_cast<DWORD>(kGatesPerChunk),
                             reinterpret_cast<DWORD64>(chunk->code))) {
        error = "Native gate RtlAddFunctionTable failed";
        return {};
    }
    chunk->registered = true;
    return chunk;
}

class SlotArena {
public:
    NativeHookSlot* allocate(void* target, std::string& error) {
        std::lock_guard lock(mutex_);
        if (!next_id_) {
            error = "Native hook ID space exhausted";
            return nullptr;
        }
        if (chunks_.empty() || chunks_.back()->used == kSlotsPerChunk) {
            auto chunk = make_chunk(error);
            if (!chunk) {
                return nullptr;
            }
            chunks_.push_back(std::move(chunk));
        }
        auto& chunk = *chunks_.back();
        auto* slot = &chunk.slots[chunk.used++];
        slot->id = next_id_++;
        slot->target = target;
        return slot;
    }

private:
    std::mutex mutex_;
    uint64_t next_id_ = 1;
    std::vector<std::unique_ptr<Chunk>> chunks_;
};

SlotArena& arena() {
    // Deliberately leaked: neither static destruction nor NativeHooks destruction may
    // invalidate a late native entrant. The pinned core owns this memory until process exit.
    static auto* instance = new SlotArena;
    return *instance;
}

} // namespace

NativeHookSlot* allocate_hook_slot(void* target, std::string& error) {
    return arena().allocate(target, error);
}

} // namespace ark::core
