# TODO20: Simple Language And Localization Plan

Status: plan approved for documentation. App changes have not started. Localization is an optional later stage.

## Goal And Scope

Make app text clear to a reader with basic English. Use short sentences and a small set of words. Keep the meaning of each control, setting, and technical operation.

Cover labels, menus, tooltips, errors, status text, help, logs, comments, project-owned code names, and related documents. Start with text that users see. Do not change audio behavior as part of a language change.

Keep `round robin` and `articulation`. The user has approved both as established technical terms. Keep their related code names and compound terms, such as `round robin ID`, `library articulation`, and `articulation slot`. Explain them when needed; do not replace them with new names.

[Project Terms](PROJECT_TERMINOLOGY_REVIEW.md) remains the single approved project dictionary. The proposals below are input for its next update, not a second approved dictionary.

## Language Basis

Use ASD-STE100 writing rules and check each general word against its approved meaning and part of speech. The standard also permits technical nouns and verbs under its rules. A short word list alone does not prove STE compliance. See the [official ASD-STE100 FAQ](https://www.asd-ste100.org/STE_faq.html).

The project rejects `derive`, `dormant`, `arbitrary`, and `persist`, including related forms, in new project wording. Record these as project restrictions. Check any claims about the standard against its dictionary before calling a specific form an STE violation.

## Initial Findings

This is a starting review, not a complete text inventory.

| Location | Current wording | Required review |
| --- | --- | --- |
| [EngineSettingsPanel.cpp](../source/UI/EngineSettingsPanel.cpp) | A loading tooltip says that Adaptive keeps extra round robins `dormant`. | Explain when extra round robins load. Keep the term `round robin`. |
| [EngineSettings.h](../source/Engine/EngineSettings.h) | Grid-size comments use `derive`. | State that zero selects the size from the active kit layout. |
| [LibraryArticulationEditorState.cpp](../source/UI/LibraryArticulationEditorState.cpp) | `persist`, `showPersistenceFailure`, and related names. | Use names that state whether changes are saved and whether saving failed. |
| [Routing assignment plan](../source/PLAN-routing-assignment-redesign.md) | `arbitrary first sample or channel`. | State the selection rule that is missing or required. |
| [TODO16](TODO16_DERIVED_SAMPLE_CACHE_AND_BAKE_CACHE_PLAN.md) | Sample-processing and disk-cache terms use restricted words. | Update terms and references together after approval. |
| [TODO19](TODO19_ADAPTIVE_SAMPLE_LOADING_PLAN.md) | Loading states use `dormant`. | Keep a clear difference between work not queued and work already in the queue. |

The source search found no use of `LocalisedStrings`, `TRANS(`, or `translate(` in `source/`. This is not proof that every text path has been checked. The bundled [JUCE translation API](../JUCE/modules/juce_core/text/juce_LocalisedStrings.h) provides text lookup and fallback support.

## Proposed Word Replacements

Choose the replacement from the actual meaning. Do not run a global word replacement.

| Restricted wording | Candidate wording | Use |
| --- | --- | --- |
| `derive`, `derived`, `deriving` | `calculate`, `get`, `create`, or `use` | State the operation: calculate a number, get a value, create data, or use a setting. |
| `derived sample` | `processed sample` | Audio after the required processing steps. |
| `dormant` | `not queued` | A prepared load description that has not entered the loading queue. Do not use this for queued work. |
| `arbitrary`, `arbitrarily` | `any`, `selected`, or the exact selection rule | Use `any` only if every choice is valid. Do not imply random selection. |
| `persist`, `persisted`, `persisting`, `persistence` | `save`, `saved`, `saving` | Writing settings or other data to storage. |
| `persistent cache` | `disk cache` | Reusable data saved on disk. |
| `persistent` in a display description | `always visible` or another exact description | A display state, not saved data. |
| `showPersistenceFailure` | `showSaveError` | Candidate C++ name for an error shown after saving fails. |
| `persist` as a Boolean argument | `saveChanges` | Candidate C++ name for choosing whether to save an edit. |

Keep old words only where the document or check must identify the restricted wording, or where an external API requires the exact name. Record the reason for each exception.

## Short Dictionary Design

Extend Project Terms after the word review. Each entry must give the preferred term, one simple meaning, and an example. Add a C++ name only where it helps link the term to the code. Keep the existing distinctions between library content, layout slots, and map layers.

These are candidate explanations, not changes to approved domain names:

| Term | Simple explanation | Example |
| --- | --- | --- |
| Articulation | A way to play an instrument. | Hit the center or the rim of a snare. |
| Round robin | One of the different recorded hits at the same velocity level. | Use different hits so repeated notes do not always play the same recording. |
| Velocity | A MIDI value that describes how hard a note is played. It is not an audio level in dB. | Use a lower value for a soft hit. |
| Sample | Recorded audio for one mic in one hit. | The room mic recording of a snare hit. |
| Mic | A microphone signal in the recording. | Direct, overhead, or room. |
| Load | Read data so the app can use it. | Load a kit. |
| Save | Write data to storage for later use. | Save an instrument map preset. |
| Reset | Return the stated settings to their default values. | Name the settings that the button will reset. |
| Cache | Data kept for reuse. State whether it is in memory or on disk. | Reuse loaded audio from the memory cache. |
| MIDI | The standard used here to send notes and control values. | A pad sends a MIDI note. |
| dB | Decibels, used for audio level or level change. | Show the unit beside a gain value. |
| ms | Milliseconds, used for time. | Show the unit beside a delay value. |

Explain short forms such as `RR` in help or a tooltip. Keep familiar technical units. Do not invent short forms to make a label fit. Give each symbol a text label or tooltip; do not use color alone to explain a state.

## Text Rules

- Use one term for one meaning. Use the same term in a label, tooltip, error, and help text.
- Use short sentences with one main point. Start action labels with the action where possible, such as `Save` or `Load`.
- State what changes and when it changes. Make the difference between an immediate edit and a saved edit clear.
- State the problem and a valid next action in an error. Do not expose internal function names as the explanation.
- Keep technical terms that users need, and explain them in simple words.
- Use complete messages. Do not construct prose from translated word fragments.
- Review all project-owned forms of restricted words, including plural forms and words inside camelCase or snake_case names.

Candidate replacement for the loading tooltip: `Default loading mode for newly loaded kits. Adaptive loads extra round robins when you play the matching articulation. Full loads all mapped samples.` Check this wording against loading behavior before applying it.

## Implementation Stages

### Stage 1: Build The Text Inventory

- List each text with its file, location, current wording, meaning, proposed wording, and review status.
- Group entries as user text, logs, comments and docs, code names, or stored identifiers.
- Start with loading settings and save errors. Then cover all pages, popups, file dialogs, status messages, and help.
- Exclude vendor code, user data, and generated files from project word changes. Record external text that users can still see.

Done when: each reviewed area has a complete list, each restricted form has a decision, and areas not yet reviewed are explicit.

### Stage 2: Approve The Dictionary

- Add the approved replacements and simple explanations to Project Terms.
- Keep `round robin` and `articulation`, including their existing code names.
- Review other established audio terms before proposing a rename.
- Record allowed short forms, units, restricted forms, and exact external-name exceptions.

Done when: each proposed replacement has a clear meaning and an approved use. No competing definitions remain between the dictionary and active plans.

### Stage 3: Update User Text

- Change text by page or feature, using the approved dictionary.
- Review labels together with their tooltips and errors.
- Check that labels still fit and that help describes the actual behavior.

Done when: the text inventory for each changed feature is complete and reviewed. App behavior is unchanged.

### Stage 4: Update Internal Names And Documents

- Rename project-owned symbols with their declarations, callers, comments, and test references in the same change.
- Update the nearest owning `AGENTS.md` and active plans when a term changes a documented contract.
- Review stored keys and file names separately. Do not translate them. If a rename is needed, update readers, writers, examples, and format docs together. No compatibility layer is required for this unreleased app.
- Keep unimplemented TODO and FIXME comments. Simplify their text without removing the task.
- If a document needs a new filename, update its links. Follow the repository rule for any removed file: move it to the workspace `.deleted/` folder.

Done when: all project references use the approved names, and no old name remains except a documented exception or a before/after example.

### Stage 5: Optional Localization

Localization means adding translated app text. First finish the English dictionary and one pilot feature. Confirm the first additional language before implementation.

- Use a small text lookup with stable keys, such as `settings.loading.help`, and an English table. Add one table per language. Reuse the bundled JUCE lookup where it meets these rules; do not add a general translation framework without a concrete need.
- Store the language choice in app settings. Proposed first version: apply the choice when an editor is next opened. State this behavior in the setting. Do not require live updates of all open editors for the first version.
- Keep the selected text table under app or editor ownership. Do not change JUCE global mappings from one plugin instance without first checking effects on other instances and JUCE dialogs.
- Use the English message when a key is missing in the selected language. A missing English key is a development error and must be found before release.
- Use named values, such as `{fileName}`, in complete messages. Check that translations keep the required value names. Use language-aware plural rules where counts appear; do not assume every language has only two forms.
- Translate user-facing engine errors at the display boundary. Keep error codes and data separate from the translated message. Keep developer logs in simple English in the first version.
- Do not translate file paths, user names for kits or presets, saved format keys, MIDI codes, or automation identifiers. Review host-visible parameter names separately from app labels.
- Load and format translated text outside the audio callback. No file reads, text lookup, or translation locks in audio processing.
- Document how to copy a language table, translate its messages, register the language, and check it. Adding a language must not require edits to individual controls.

Done when: the pilot feature works in English and the selected second language, missing translations show English, and another table can be added using the documented steps.

### Stage 6: Verify And Keep Text Clear

- Review restricted forms with a small explicit list and exact exceptions. Split compound code names when checking them. Do not treat substring matches or a clean word scan as proof of STE compliance.
- Check changed labels, tooltips, errors, and saved-setting descriptions against behavior.
- For localization, check missing keys, duplicate keys, named values, plural forms, non-Latin text, and font support. Use expanded text to find clipped labels and buttons.
- Check two plugin instances, closing and reopening editors, language settings, and English fallback.
- Request builds and tests only when the user asks for them. These checks are planned, not run as part of this document task.

Done when: all required review items pass and any remaining exception has an owner and a reason. Mark completed stages with ✅. Use `# ✅ COMPLETED: ` for the main header only after all required work is complete and the optional stage is complete or explicitly deferred.

## Next Step

Start Stage 1 after the user requests implementation. The first review should cover loading settings and save errors. Approve concrete word changes before editing app code. No additional language choice is needed until Stage 5 is selected.
