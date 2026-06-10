# DIDITAGAIN STUDIO - AI Texture Demo Pack

This pack contains three cached AI Texture v0.1 demo presets and matching loopable WAV texture layers.

Install by copying the folders into your DIDITAGAIN STUDIO Documents root so paths resolve like:

{DIDA_DOCS}/Presets/User/AI Texture/*.diapreset
{DIDA_DOCS}/NeuralTextures/Demo/.../*.wav

Included presets:
- AI Brass Air Test: breathy brass air/noise layer
- AI Choir Ghost Test: wide vowel/ghost choir layer
- AI Guitar Dust Test: fret/scrape/tape dust layer

These are cached neural-style texture assets, not live RAVE/DDSP inference. The plugin should treat them as neuralTextureCached partials.

Expected quality flags when loaded correctly:
AI_TEXTURE_ENABLED, AI_TEXTURE_PARTIAL_PRESENT, AI_TEXTURE_IMPORTED_OK, AI_TEXTURE_FREEZE_READY, AI_TEXTURE_RENDERING_CACHED.

If your real multisample paths differ, keep the neuralTextureCached partial and replace the normal sample/source section with your actual instrument mapping.
