#pragma once
#include "terminal.hpp"
#include <stdexcept>
#include <chrono>
#include <thread>
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline std::string rowText(Terminal& term, int row = 0) {
    term.render();
    GhosttyRenderStateRowIterator rows{};
    GhosttyRenderStateRowCells cells{};
    require(ghostty_render_state_row_iterator_new(nullptr, &rows) == GHOSTTY_SUCCESS, "row allocation");
    require(ghostty_render_state_row_cells_new(nullptr, &cells) == GHOSTTY_SUCCESS, "cell allocation");
    ghostty_render_state_get(term.state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rows);
    for (int i = 0; i <= row; ++i) require(ghostty_render_state_row_iterator_next(rows), "missing row");
    ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells);
    std::string text;
    while (ghostty_render_state_row_cells_next(cells)) {
        char bytes[256];
        GhosttyBuffer buf{reinterpret_cast<uint8_t*>(bytes), sizeof(bytes), 0};
        auto result = ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buf);
        require(result == GHOSTTY_SUCCESS, "grapheme UTF-8 extraction");
        if (buf.len) text.append(bytes, buf.len); else text += ' ';
    }
    ghostty_render_state_row_cells_free(cells);
    ghostty_render_state_row_iterator_free(rows);
    return text;
}
template<typename Predicate> inline void waitUntil(Predicate predicate, const char* message) {
    for (int i = 0; i < 300; ++i) {
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error(message);
}
