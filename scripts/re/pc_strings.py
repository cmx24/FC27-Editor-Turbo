"""Locate the player-capture related strings in the image and print their VAs (step 1 of the player_capture RE)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx
names = ["FE::FIFA::PlayerCaptureRequest", "FE::FIFA::PlayerCaptureInit", "PlayerCaptureController", "PlayerCaptureStream",
         "YouthPlayerManager::PortraitCaptureExecutor", "data/ui/imgAssets/playerCapture/p%s.png", "captureManagerAvatar",
         "avatar.headshot", "EnablePlayerCapture", "CareerModeStateInitPlayerCaptures", "CareerModeStateUpdatePlayerCaptures",
         "DynamicPortrait", "HasDynamicPortrait", "IsDynamicPortraitOnDisk", "DYNAMIC_PORTRAIT_DOWNLOAD_COMPLETE",
         "playerCaptureController", "DynTifoPlayerCaptureRequest", "PlayerCapture", "playerCapture"]
for n in names:
    print("%-48s %s" % (n, " ".join("%x" % v for v in rx.find_str(n))))
print("--- substrings (distinct C strings)")
seen = set()
for sub in ["PlayerCapture", "playerCapture", "PortraitCapture", "Headshot", "headshot", "ManagerAvatar", "managerAvatar", "Portrait"]:
    for va in rx.find_substr(sub, limit=600):
        s = rx.cstr(va, 160)
        if s in seen or len(s) > 150: continue
        seen.add(s)
        print("%x  %s" % (va, s))
