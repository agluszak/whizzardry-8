#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

struct GameStatus;
struct Level;
struct W8Chunk;
struct W8LoadedLevelStatus;
struct W8WorldItem;
struct World;

/* A level's saved state as its LVLS group holds it: decoded from a status file or a stored
   group, or captured from the level's worlds, without the level's graphics or scripts. */
class LevelState {
public:
    struct Data;

    /* Level `level`'s group in a status file, or nothing when the file is missing or has
       none. Throws on malformed input. */
    static std::optional<LevelState> decode(const std::filesystem::path& path, int level,
                                            bool from_assets = false);
    // A group stored as a RIFF image of its own (LeftLevels). Throws on malformed input.
    static std::optional<LevelState> decode(std::span<const std::byte> image, int level);
    // The state a save keeps of the loaded level, from its worlds and the game's status.
    static LevelState capture(const Level& level, const GameStatus& status);

    LevelState(LevelState&&) noexcept;
    LevelState& operator=(LevelState&&) noexcept;
    ~LevelState();

    int level() const;
    /* Write the state as the LVLS group it came from; the asset status's cursor nodes and
       encounters are not written. */
    void encode(W8Chunk& chunks) const;
    /* Build the unpublished `world`'s monsters, items and saved state, loading their
       representations and scripts. Throws when the state names what the game data lacks;
       the world is then discarded. */
    std::unique_ptr<W8LoadedLevelStatus> build(World& world) &&;
    // Throws when a monster or group names a species the monster database lacks.
    void checkSpecies() const;
    // The world items alone, unlisted (the camp chest takes them from another level).
    std::vector<std::unique_ptr<W8WorldItem>> buildItems() const;

private:
    LevelState();
    static std::optional<LevelState> decode(W8Chunk& chunk, int level, bool from_assets);

    std::unique_ptr<Data> data_;
};

/* The saved state of each level the party has left (or a load brought), kept undecoded as
   its LVLS group, so a level the party never enters again is saved exactly as it came. */
class LeftLevels {
public:
    // Keep `state` for when the party returns to its level.
    void store(const LevelState& state);
    // Keep a load's group image of `level` as it is.
    void keep(int level, std::vector<std::byte> image);
    // The level's state as the party left it, or nothing when it never left the level.
    std::optional<LevelState> decode(int level) const;
    // Copy every stored group but `current`'s into a save.
    void write(W8Chunk& chunks, int current) const;

private:
    std::map<int, std::vector<std::byte>> images_;
};
