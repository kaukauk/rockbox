# TODO — `audiobook-mods` branch

Tracking loose ends from the audiobook-mode work + things noticed in
passing on the Y1 / rockbox-y1 fork.

## Audiobook feature gaps

- [ ] **Text-setting menu items for `audiobook path` / `audiobook
      extensions`.** Currently both are config.cfg-only because plain
      `MENUITEM_SETTING` on a `TEXT_SETTING` crashes inside the skin
      engine (segv in `evaluate_conditional`). Right fix is a
      `MENUITEM_FUNCTION` that pops a `kbd_input` dialog, persists via
      `settings_save()`. Existing `start_directory` reset item is the
      closest pattern to copy.
- [ ] **Auto-bookmark on playlist replacement (audiobook only).**
      Today `bookmark_autocreate` only fires from `audio_stop` /
      `audio_stop_recording` / WPS-stop, so navigating *away* from a
      playing audiobook without pressing Stop loses the position.
      Hook somewhere in the "load a new playlist" path
      (`playlist_create` / `audio_play_start` / playlist clear) to
      call `bookmark_autobookmark(false)` *iff* the outgoing track is
      an audiobook (`audiobook_is_active()` is still correct here —
      outgoing context).
- [ ] **Auto-enable `runtimedb` when `audiobook_mode` is on.**
      `audiobook_autoresume_enable: true` is a no-op without
      `gather runtime data: on` because the resume position is stored
      in the runtime database. Either gate the audiobook menu item on
      runtimedb being on (with a hint), or transparently enable
      runtimedb when audiobook_mode is toggled on.
- [ ] **Per-extension list parser** doesn't validate; pasting
      `mp3, m4b ,  m4a` works but `mp3;m4b` and `mp3 m4b` may not. Add
      a quick test in `extension_in_list` for the separators we
      actually claim to support (currently only comma reliably).
- [ ] **No translation strings beyond `LANG_AUDIOBOOK_SETTINGS` /
      `LANG_AUDIOBOOK_MODE`.** The six audiobook value rows borrow
      existing labels (e.g. `LANG_SKIP_LENGTH`), so in the menu they
      appear with the same name as the *global* row — visually
      ambiguous. Worth adding distinct `LANG_AUDIOBOOK_SKIP_LENGTH`,
      etc., once we know which strings the upstream fork would accept.

## Adjacent bugs noticed

- [ ] **Filename-prefix double-prepending in `cfg_to_string`** —
      patched in commit `acb5829228` but only for the *save* side.
      The complementary fix would be in `copy_filename_setting`:
      recognise a legacy `/.rockbox/<sub>/` prefix and strip it on
      load. Without that, the stored value remains an absolute path
      and re-renders the same way every save — works but is messier
      in config.cfg.
- [ ] **`adb install -r` on the Y1 doesn't repopulate
      `/data/app-lib/org.rockbox-N/`** because Rockbox is installed
      as a system app at `/system/app/org.rockbox.apk`. Codec libs
      (e.g. `libmpa.so`) silently vanish; symptom is "any track
      finishes immediately, WPS bounces back to browser". Workaround
      is to `adb push` `lib/armeabi/*.so` from the APK into the
      symlink target manually. A proper fix in the build/install
      flow would either:
        - install via the Innioasis Updater path (system-app aware), or
        - have `rb-diag.sh` (or a new `rb-install.sh`) wrap
          `adb install -r` + the lib-push, every time.
- [ ] **CabbieV2 default font (24-pt Terminus) and 32×32 Tango
      icons** are visually tiny on the 480×360 panel. Not a code
      bug, but the upstream nightly ships CabbieV2 as the fallback
      theme — first-boot UX is rough. A small `cabbiev2.cfg` override
      pointing at a 30-pt font + 48×48 iconset would help. Same
      applies to `rockbox_default`.
- [ ] **Theme-load path bug**: at some point the running theme's
      saved paths in `config.cfg` got the doubled-prefix corruption
      we patched. Even after the patch, the *historic* corruption
      persists in the cfg file. A one-shot "scan config.cfg on boot
      and strip doubled prefixes" migration would self-heal old
      installs — currently the user has to hand-edit or re-pick the
      theme.

## Process / repo

- [ ] **`building.md` says `--target=201`** but at the
      `nightly-051dece4e4` tag the Y1 is target `310` (`201` is the
      generic Android target). Builds silently produce a wrong
      binary (no `INNIOASIS_Y1` define → wrong `ROCKBOX_DIR` path
      handling → Satellite theme regression). Either update the doc,
      or have `tools/configure` reject `--target=201 --lcdwidth=480
      --lcdheight=360` for hosted-Android builds.
- [ ] **`y1` branch is rebased away from the published nightly tags.**
      `git log nightly-051dece4e4..y1` returns ~210 commits while
      `git log y1..nightly-051dece4e4` returns ~903; the two
      lineages have diverged. Contributors targeting "what's on my
      device" have to checkout the tag, not the branch. Worth a
      release-engineering note (or a `release-2026.05` branch that
      tracks each nightly).
