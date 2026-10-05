# Vivid Fever Dreams info controls

The five plugins use `VfdInfo.h` and the supplied `VfdLogo.svg` from this directory. Each plugin adds the SVG to its existing JUCE binary data target and passes its Chakra Petch typeface to the shared components. The original logo's path geometry is preserved; only its display color is adapted to the plugin palette.

`InfoButton` paints the dedicated molded INFO key. `InfoOverlay` owns the logo, credits, external links and close control. Opening or closing this component never changes processor state. The page intercepts pointer input, contains keyboard focus while open, closes on Escape, and returns focus to the invoking key.

The plugin projects must remain siblings of this directory when building from source. The shared component is independent of their audio processors and parameter models. Plugin-specific chassis placement stays in each editor.

The approved directions are implemented by preserving the existing hardware header stripes, keeping the logo off the physical key, and showing the complete logo as the hero of the info page.

External destinations:

- Executive producer @cxy: https://x.com/cxy
- Executive producer @clt: https://x.com/clt
- Music: https://www.vividfeverdreams.xyz
- Donations: https://fundraising.fracturedatlas.org/dream-sequence

The integration checks validate the four destinations without opening browsers or submitting anything to the external sites.
