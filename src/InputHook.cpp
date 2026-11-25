#include "InputHook.h"

#include "GameIDs.h"

namespace DROP::InputHook
{
	namespace
	{
		using Process_t = void (*)(void* a_controls, RE::InputEvent* a_head);

		Process_t            g_original = nullptr;
		std::atomic<FrameFn> g_frame{ nullptr };

		// Startup check that the hook runs as expected: about once a frame.
		void CheckRate()
		{
			using clock = std::chrono::steady_clock;
			static std::uint32_t     thread = 0;
			static clock::time_point start{};
			static std::uint64_t     calls = 0;
			static bool              reported = false;
			static bool              otherThread = false;

			const auto current = REX::W32::GetCurrentThreadId();
			if (calls++ == 0) {
				thread = current;
				start = clock::now();
				REX::INFO("Frame hook running");
				return;
			}
			if (current != thread && !otherThread) {
				otherThread = true;
				REX::INFO("Frame hook also runs on thread {} (first was {}): the game moves its frame work between threads", current, thread);
			}
			const float seconds = std::chrono::duration<float>(clock::now() - start).count();
			if (!reported) {
				if (seconds >= 10.0F) {
					reported = true;
					REX::INFO("Frame hook: {} calls in {:.0f} s (about {:.0f} a second)", calls, seconds, static_cast<float>(calls) / seconds);
				}
			} else if (seconds < 600.0F) {
				// For the first 10 minutes, a line a minute shows the hook keeps running.
				static int minutes = 0;
				if (static_cast<int>(seconds / 60.0F) > minutes) {
					minutes = static_cast<int>(seconds / 60.0F);
					REX::INFO("Frame hook still running ({} calls so far)", calls);
				}
			}
		}

		void Hook(void* a_controls, RE::InputEvent* a_head)
		{
			g_original(a_controls, a_head);

			const auto frame = g_frame.load(std::memory_order_acquire);
			if (!frame) {
				return;
			}
			CheckRate();
			try {
				frame(a_head);
			} catch (const std::exception& e) {
				static bool logged = false;
				if (!logged) {
					logged = true;
					REX::ERROR("Frame update failed: {}", e.what());
				}
			} catch (...) {
				static bool logged = false;
				if (!logged) {
					logged = true;
					REX::ERROR("Frame update failed");
				}
			}
		}

		bool InstallJump(std::uintptr_t a_target)
		{
			using namespace GameID;
			if (std::memcmp(reinterpret_cast<const void*>(a_target), kInputProcessingBytes.data(), kInputProcessingBytes.size()) != 0) {
				return false;
			}

			auto& trampoline = REL::GetTrampoline();

			// Stub that runs the original function: the 5 overwritten bytes, then a jump back past them.
			constexpr std::size_t kJmpSize = 6 + sizeof(std::uint64_t);
			auto* stub = static_cast<std::uint8_t*>(trampoline.allocate(kInputProcessingPrologue + kJmpSize));
			std::memcpy(stub, kInputProcessingBytes.data(), kInputProcessingPrologue);
			const std::uint8_t jmpAbs[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
			std::memcpy(stub + kInputProcessingPrologue, jmpAbs, sizeof(jmpAbs));
			const std::uint64_t back = a_target + kInputProcessingPrologue;
			std::memcpy(stub + kInputProcessingPrologue + sizeof(jmpAbs), &back, sizeof(back));
			g_original = reinterpret_cast<Process_t>(stub);

			trampoline.write_jmp<5>(a_target, &Hook);
			return true;
		}
	}

	bool Install()
	{
		using namespace GameID;
		const auto target = PerformInputProcessing.address();

		if (InstallJump(target)) {
			REX::INFO("Frame hook installed at {:X}", target);
			return true;
		}

		// Another plugin changed the start of the function. Chain through the vtable slot instead.
		REL::Relocation<std::uintptr_t> vtable{ PlayerControlsVtable };
		const auto current = *reinterpret_cast<const std::uintptr_t*>(vtable.address() + sizeof(void*) * kInputProcessingSlot);
		if (!current) {
			REX::ERROR("PlayerControls input pass not found; the plugin stays inactive");
			return false;
		}
		g_original = reinterpret_cast<Process_t>(vtable.write_vfunc(kInputProcessingSlot, &Hook));
		REX::WARN("The start of the PlayerControls input pass ({:X}) isn't what this version expects (another mod "
				  "changes it?); hooked its vtable slot instead{}. Plugins that check that slot and load after this "
				  "one (such as Dialogue Movement Enabler) may then refuse to install.",
			target, current == target ? "" : ", which another mod had already hooked");
		return true;
	}

	void SetFrameCallback(FrameFn a_fn)
	{
		g_frame.store(a_fn, std::memory_order_release);
	}
}
