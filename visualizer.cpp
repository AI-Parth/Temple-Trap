// visualizer.cpp — Temple Trap SFML visualizer
// Reads the JSON solution path produced by temple_solver and renders it
// in a resizable window with full playback controls.

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ─── Data model ──────────────────────────────────────────────────────────────

struct TileData {
    uint8_t     id;
    std::string name;
    uint8_t     stair_lvl;
};

struct StepData {
    std::array<TileData, 9> tiles;
    int pawn_pos;
    int blank_pos;
};

// ─── Tile open-side bitmasks (mirrors solver.cpp TILE_INFO) ──────────────────
//   Bit 0 = top, 1 = right, 2 = bottom, 3 = left
static constexpr uint8_t OPEN_MASK[15] = {
    0b0011u, // A
    0b0110u, // B
    0b1100u, // A1
    0b1001u, // B1
    0b0101u, // C
    0b1010u, // C1
    0b0001u, // D  (stair face=top)
    0b0010u, // E  (stair face=right)
    0b0100u, // D1 (stair face=bot)
    0b1000u, // E1 (stair face=left)
    0b0011u, // F
    0b0110u, // G
    0b1100u, // H
    0b1001u, // I
    0b0000u, // BL
};

// ─── Minimal JSON parser ──────────────────────────────────────────────────────
// Scans the solver's output JSON without a full parser library.

static std::string slurp(const std::string& filename) {
    std::ifstream f(filename);
    if (!f) throw std::runtime_error("Cannot open: " + filename);
    return { (std::istreambuf_iterator<char>(f)),
              std::istreambuf_iterator<char>() };
}

// Advance `pos` past whitespace in `s`.
static void skip_ws(const std::string& s, std::size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                               s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
}

// Return the integer that follows the first occurrence of `"key":` from `pos`.
// Updates `pos` to just after the integer.
static int find_int(const std::string& s, const std::string& key,
                    std::size_t& pos) {
    std::string needle = "\"" + key + "\"";
    std::size_t p = s.find(needle, pos);
    if (p == std::string::npos)
        throw std::runtime_error("JSON key not found: " + key);
    p += needle.size();
    while (p < s.size() && s[p] != ':') ++p;
    ++p;
    skip_ws(s, p);
    bool neg = (s[p] == '-');
    if (neg) ++p;
    int val = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
        val = val * 10 + (s[p++] - '0');
    }
    pos = p;
    return neg ? -val : val;
}

// Return the quoted string that follows the first occurrence of `"key":`.
static std::string find_str(const std::string& s, const std::string& key,
                             std::size_t& pos) {
    std::string needle = "\"" + key + "\"";
    std::size_t p = s.find(needle, pos);
    if (p == std::string::npos)
        throw std::runtime_error("JSON key not found: " + key);
    p += needle.size();
    while (p < s.size() && s[p] != ':') ++p;
    ++p;
    skip_ws(s, p);
    if (s[p] != '"') throw std::runtime_error("Expected string for key: " + key);
    ++p;
    std::string result;
    while (p < s.size() && s[p] != '"') result += s[p++];
    ++p;
    pos = p;
    return result;
}

std::vector<StepData> load_path(const std::string& filename) {
    const std::string json = slurp(filename);
    std::vector<StepData> steps;

    // Find the opening "[" of the "steps" array.
    std::size_t pos = 0;
    {
        std::size_t arr = json.find("\"steps\"", pos);
        if (arr == std::string::npos)
            throw std::runtime_error("No 'steps' array in JSON");
        arr = json.find('[', arr);
        if (arr == std::string::npos)
            throw std::runtime_error("Malformed JSON: no '[' after steps");
        pos = arr + 1;
    }

    // Iterate over step objects  { "pawn": N, "blank": N, "tiles": [...] }
    while (true) {
        skip_ws(json, pos);
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '{') { ++pos; continue; }

        // Find the closing '}' for this step object to bound our searches.
        // (We just search forward – works for our flat structure.)

        StepData step;

        std::size_t step_start = pos;
        step.pawn_pos  = find_int(json, "pawn",  step_start);
        step.blank_pos = find_int(json, "blank", step_start);

        // Locate the "tiles" array.
        std::size_t ta = json.find("\"tiles\"", pos);
        if (ta == std::string::npos) break;
        ta = json.find('[', ta);
        if (ta == std::string::npos) break;
        std::size_t tile_pos = ta + 1;

        for (int i = 0; i < 9; ++i) {
            // Find the opening '{' of the i-th tile object.
            std::size_t to = json.find('{', tile_pos);
            if (to == std::string::npos) break;
            tile_pos = to + 1;

            std::size_t tp = tile_pos;
            step.tiles[i].id        = static_cast<uint8_t>(find_int(json, "id",   tp));
            step.tiles[i].name      = find_str(json, "name", tp);
            step.tiles[i].stair_lvl = static_cast<uint8_t>(find_int(json, "slvl", tp));
            tile_pos = tp;
        }

        // Advance `pos` past this step's closing '}'.
        std::size_t close = json.find('}', tile_pos);
        if (close == std::string::npos) break;
        close = json.find('}', close + 1);  // outer step '}'
        if (close == std::string::npos) break;
        pos = close + 1;

        steps.push_back(std::move(step));
    }

    if (steps.empty())
        throw std::runtime_error("No steps parsed from " + filename);

    return steps;
}

// ─── Colour palette ───────────────────────────────────────────────────────────

static const sf::Color C_BG       { 15,  15,  20 };
static const sf::Color C_GROUND   {140,  80,  20 };
static const sf::Color C_TOP      { 40, 140,  50 };
static const sf::Color C_STAIR    { 30, 120, 220 };
static const sf::Color C_BLANK    { 30,  30,  35 };
static const sf::Color C_OPENING  {255, 245, 180 };
static const sf::Color C_PAWN     {255, 255, 255 };
static const sf::Color C_PAWN_OUT {  0,  80, 180 };
static const sf::Color C_LABEL    {  0,   0,   0 };
static const sf::Color C_GOAL_HL  {255, 215,   0 };  // gold highlight for goal tile
static const sf::Color C_UI_TEXT  {220, 220, 220 };
static const sf::Color C_UI_BG    { 25,  25,  30 };

// ─── Font loading ─────────────────────────────────────────────────────────────

static bool try_load_font(sf::Font& font) {
    static const char* candidates[] = {
        // Linux
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        // macOS
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        // Windows
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        // Local fallback
        "arial.ttf",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        if (font.loadFromFile(candidates[i])) return true;
    }
    return false;
}

// ─── Drawing helpers ──────────────────────────────────────────────────────────

static void draw_thick_line(sf::RenderWindow& win,
                             float x1, float y1, float x2, float y2,
                             float thickness, sf::Color col) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    sf::VertexArray va(sf::Quads, 4);
    va[0].position = {x1 + nx, y1 + ny};
    va[1].position = {x1 - nx, y1 - ny};
    va[2].position = {x2 - nx, y2 - ny};
    va[3].position = {x2 + nx, y2 + ny};
    for (int i = 0; i < 4; ++i) va[i].color = col;
    win.draw(va);
}

static void draw_rounded_rect(sf::RenderWindow& win,
                               float x, float y, float w, float h,
                               float radius, sf::Color fill) {
    // Approximate rounded rect with a centre rect + 4 edge rects + 4 corner circles.
    radius = std::min(radius, std::min(w, h) * 0.5f);

    sf::RectangleShape r;
    r.setFillColor(fill);

    // Centre
    r.setSize({w - 2*radius, h - 2*radius});
    r.setPosition(x + radius, y + radius);
    win.draw(r);
    // Top & bottom bars
    r.setSize({w - 2*radius, radius});
    r.setPosition(x + radius, y);           win.draw(r);
    r.setPosition(x + radius, y + h - radius); win.draw(r);
    // Left & right bars
    r.setSize({radius, h - 2*radius});
    r.setPosition(x, y + radius);           win.draw(r);
    r.setPosition(x + w - radius, y + radius); win.draw(r);
    // Corners
    sf::CircleShape c(radius);
    c.setFillColor(fill);
    c.setOrigin(radius, radius);
    c.setPosition(x + radius,       y + radius);       win.draw(c);
    c.setPosition(x + w - radius,   y + radius);       win.draw(c);
    c.setPosition(x + radius,       y + h - radius);   win.draw(c);
    c.setPosition(x + w - radius,   y + h - radius);   win.draw(c);
}

// ─── TempleVisualizer ─────────────────────────────────────────────────────────

class TempleVisualizer {
public:
    explicit TempleVisualizer(std::vector<StepData> path)
        : m_path(std::move(path)) {}

    void run() {
        constexpr int WIN_W = 540;
        constexpr int WIN_H = 600;  // 540 board + 60 UI strip

        sf::RenderWindow window(sf::VideoMode(WIN_W, WIN_H), "Temple Trap",
                                sf::Style::Titlebar | sf::Style::Close);
        window.setFramerateLimit(60);

        const bool has_font = try_load_font(m_font);
        if (!has_font)
            std::fputs("[visualizer] Warning: no system font found; text disabled.\n",
                       stderr);

        sf::Clock clock;
        float     time_acc = 0.0f;

        while (window.isOpen()) {
            // ── Event handling ────────────────────────────────────────────────
            sf::Event event{};
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed) {
                    window.close();
                } else if (event.type == sf::Event::KeyPressed) {
                    on_key(event.key.code);
                }
            }

            // ── Playback tick ─────────────────────────────────────────────────
            const float dt = clock.restart().asSeconds();
            if (m_playing) {
                time_acc += dt;
                const float frame_dur = 1.0f / m_speed;
                while (time_acc >= frame_dur) {
                    time_acc -= frame_dur;
                    if (m_step + 1 < static_cast<int>(m_path.size())) {
                        ++m_step;
                    } else {
                        m_playing = false;
                        time_acc  = 0.0f;
                    }
                }
            } else {
                time_acc = 0.0f;
            }

            // ── Render ────────────────────────────────────────────────────────
            window.clear(C_BG);
            draw_board(window, m_path[m_step]);
            draw_ui(window, has_font);
            window.display();
        }
    }

private:
    std::vector<StepData> m_path;
    sf::Font m_font;
    int      m_step    = 0;
    bool     m_playing = false;
    float    m_speed   = 2.0f;  // steps per second

    // ── Key bindings ──────────────────────────────────────────────────────────
    void on_key(sf::Keyboard::Key key) {
        switch (key) {
        case sf::Keyboard::Space:
            m_playing = !m_playing;
            break;
        case sf::Keyboard::R:
            m_step    = 0;
            m_playing = false;
            break;
        case sf::Keyboard::Right:
            if (m_step + 1 < static_cast<int>(m_path.size())) ++m_step;
            break;
        case sf::Keyboard::Left:
            if (m_step > 0) --m_step;
            break;
        case sf::Keyboard::Up:
            m_speed = std::min(m_speed * 1.5f, 30.0f);
            break;
        case sf::Keyboard::Down:
            m_speed = std::max(m_speed / 1.5f, 0.25f);
            break;
        default:
            break;
        }
    }

    // ── Board rendering ───────────────────────────────────────────────────────
    void draw_board(sf::RenderWindow& win, const StepData& step) const {
        constexpr float TILE  = 180.0f;
        constexpr float PAD   = 5.0f;
        constexpr float INNER = TILE - 2.0f * PAD;
        constexpr float RAD   = 14.0f;
        constexpr float MARG  = 10.0f;  // gap from opening to edge
        constexpr float THICK = 9.0f;   // opening line thickness

        for (int i = 0; i < 9; ++i) {
            const float col_f = static_cast<float>(i % 3);
            const float row_f = static_cast<float>(i / 3);
            const float x     = col_f * TILE + PAD;
            const float y     = row_f * TILE + PAD;
            const float cx    = x + INNER * 0.5f;
            const float cy    = y + INNER * 0.5f;

            const TileData& td = step.tiles[i];

            // ── Tile background ───────────────────────────────────────────────
            sf::Color bg = C_BLANK;
            if (td.id != 14 /*BL*/) {
                if (td.id >= 6 && td.id <= 9)       bg = C_STAIR;    // stairs
                else if (td.id <= 5)                 bg = C_TOP;      // level 1
                else                                 bg = C_GROUND;   // level 0
            }
            // Highlight goal tile at position 0 when pawn is there
            if (i == 0 && step.pawn_pos == 0)        bg = C_GOAL_HL;

            draw_rounded_rect(win, x, y, INNER, INNER, RAD, bg);

            if (td.id == 14) continue;  // blank: nothing more to draw

            // ── Opening lines ─────────────────────────────────────────────────
            // Each opening is a line from the tile edge into the centre.
            const uint8_t mask = OPEN_MASK[td.id];
            if (mask & 0x1u)  // top
                draw_thick_line(win, cx, y + MARG, cx, cy, THICK, C_OPENING);
            if (mask & 0x2u)  // right
                draw_thick_line(win, x + INNER - MARG, cy, cx, cy, THICK, C_OPENING);
            if (mask & 0x4u)  // bottom
                draw_thick_line(win, cx, y + INNER - MARG, cx, cy, THICK, C_OPENING);
            if (mask & 0x8u)  // left
                draw_thick_line(win, x + MARG, cy, cx, cy, THICK, C_OPENING);

            // ── Stair level indicator (small dot) ─────────────────────────────
            if (td.id >= 6 && td.id <= 9) {
                sf::CircleShape dot(5.0f);
                dot.setFillColor(td.stair_lvl == 1 ? C_TOP : C_GROUND);
                dot.setOrigin(5.0f, 5.0f);
                dot.setPosition(x + INNER - 18.0f, y + 14.0f);
                win.draw(dot);
            }

            // ── Tile name label ───────────────────────────────────────────────
            if (m_font.getInfo().family.empty()) {
                // no font loaded — skip text
            } else {
                sf::Text label(td.name, m_font, 16);
                label.setFillColor(C_LABEL);
                label.setStyle(sf::Text::Bold);
                const auto bounds = label.getLocalBounds();
                label.setOrigin(bounds.left + bounds.width  * 0.5f,
                                bounds.top  + bounds.height * 0.5f);
                label.setPosition(x + 24.0f, y + 18.0f);
                win.draw(label);
            }

            // ── Pawn ──────────────────────────────────────────────────────────
            if (i == step.pawn_pos) {
                constexpr float R = 30.0f;
                // Outer ring
                sf::CircleShape ring(R + 4.0f);
                ring.setFillColor(C_PAWN_OUT);
                ring.setOrigin(R + 4.0f, R + 4.0f);
                ring.setPosition(cx, cy);
                win.draw(ring);
                // Inner fill
                sf::CircleShape pawn(R);
                pawn.setFillColor(C_PAWN);
                pawn.setOrigin(R, R);
                pawn.setPosition(cx, cy);
                win.draw(pawn);
            }
        }
    }

    // ── UI strip at the bottom ────────────────────────────────────────────────
    void draw_ui(sf::RenderWindow& win, bool has_font) const {
        constexpr float BOARD_H = 540.0f;
        constexpr float UI_H    =  60.0f;

        sf::RectangleShape bar({540.0f, UI_H});
        bar.setPosition(0.0f, BOARD_H);
        bar.setFillColor(C_UI_BG);
        win.draw(bar);

        if (!has_font) return;

        // ── Progress bar ──────────────────────────────────────────────────────
        const int   total  = static_cast<int>(m_path.size()) - 1;
        const float frac   = total > 0 ? static_cast<float>(m_step) / total : 0.0f;
        constexpr float BX = 10.0f, BY = BOARD_H + 8.0f, BW = 520.0f, BH = 8.0f;

        sf::RectangleShape bg_bar({BW, BH});
        bg_bar.setPosition(BX, BY);
        bg_bar.setFillColor(sf::Color(60, 60, 70));
        win.draw(bg_bar);

        if (frac > 0.0f) {
            sf::RectangleShape fill_bar({BW * frac, BH});
            fill_bar.setPosition(BX, BY);
            fill_bar.setFillColor(sf::Color(80, 180, 255));
            win.draw(fill_bar);
        }

        // ── Status text ───────────────────────────────────────────────────────
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "%s  Step %d / %d   Speed: %.1fx   "
                      "[Space] Play/Pause  [←→] Step  [↑↓] Speed  [R] Restart",
                      m_playing ? "▶" : "⏸", m_step, total, m_speed);

        sf::Text status(buf, m_font, 13);
        status.setFillColor(C_UI_TEXT);
        status.setPosition(BX, BOARD_H + 22.0f);
        win.draw(status);
    }
};

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const std::string path_file =
        (argc >= 2) ? argv[1] : "temple_path.json";

    std::vector<StepData> path;
    try {
        path = load_path(path_file);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error loading path: %s\n", e.what());
        std::fprintf(stderr, "Usage: temple_visualizer [path_file.json]\n");
        return 1;
    }

    std::printf("Loaded %zu steps from %s\n", path.size(), path_file.c_str());

    TempleVisualizer viz(std::move(path));
    viz.run();
    return 0;
}
