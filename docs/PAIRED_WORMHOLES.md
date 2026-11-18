# Saturn and Gargantua wormholes

The Saturn mouth and Gargantua mouth are a fictional, traversable pair. Both use the existing rotating Damour–Solodukhin optical geometry, a 1,000 km throat radius and the same geometric spin/deformation parameters. Neither mouth is a luminous surface or an absorbing black-hole horizon.

The Solar mouth retains its authored Saturn satellite orbit. The Gargantua mouth follows an illustrative inclined orbit with a 300 AU semimajor axis and 0.02 eccentricity. This placement is an engine scene choice, not a claim about the movie's orbital data. The Gargantua mouth is selectable and searchable as **Gargantua Wormhole**.

The view through each mouth uses a cached floating-point destination environment. Saturn shows the Gargantua sky and accretion disc; Gargantua shows the Solar System from the Saturn mouth. Captures omit the portal itself to prevent recursive rendering. The 512-pixel cube faces refresh once per simulated hour, avoiding a second full scene render every frame. Background capture does not regenerate or evict the active camera's streamed star cells.

The authored exterior rotation is shared between optical rendering and camera transport; the reverse mouth uses its transpose. Local foreground occlusion is compared against the engine's logarithmic depth encoding. The remote view remains nonemissive, so the mouth does not acquire a star's camera flare.

Fly into either throat to travel to its partner. A swept segment test detects crossings even if the camera moves across the entire mouth in one frame, including motion of the source mouth. View direction and up are transformed by the shared exterior rotation; flight speed, field of view and world time carry across. Arrival is outside the other throat, with a brief cooldown to prevent immediate return. A bounded remainder prevents a single warp-speed frame from skipping far past the destination. Passing beside a mouth does not trigger transport.

The object finder exposes **Saturn Kerr Wormhole** under Saturn and **Gargantua Wormhole** under Gargantua. Select a mouth and use **Go to selection** to approach it, then fly forward. These scene connections are fictional and separate from black-hole horizon entry.
