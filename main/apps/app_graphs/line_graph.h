/**
 * @file line_graph.h
 * @author d4rkmen
 * @brief Reusable line graph rendering component
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <stdint.h>
#include <vector>
#include "mesh/mesh_data.h"
#include "LovyanGFX.hpp"

namespace MOONCAKE::APPS
{

    class LineGraph
    {
    public:
        LineGraph() = default;
        ~LineGraph() = default;

        /**
         * @brief Set the data points to display
         * @param points Vector of graph points
         */
        void setData(const std::vector<Mesh::GraphPoint>& points);

        /**
         * @brief Set Y-axis range (auto-scales if not set)
         * @param min_y Minimum Y value
         * @param max_y Maximum Y value
         */
        void setYRange(float min_y, float max_y);

        /**
         * @brief Enable auto-scaling for Y-axis
         */
        void setAutoScale(bool enabled) { _auto_scale = enabled; }

        /**
         * @brief Set line color
         * @param color RGB565 color
         */
        void setLineColor(uint16_t color) { _line_color = color; }

        /**
         * @brief Set grid color
         * @param color RGB565 color
         */
        void setGridColor(uint16_t color) { _grid_color = color; }

        /**
         * @brief Set background color
         * @param color RGB565 color
         */
        void setBgColor(uint32_t color) { _bg_color = color; }

        /**
         * @brief Set number of horizontal grid lines
         * @param count Number of grid lines (0 to disable)
         */
        void setGridLines(int count) { _grid_lines = count; }

        /**
         * @brief Render the graph to a canvas
         * @param canvas LGFX canvas to draw on
         * @param x X position
         * @param y Y position  
         * @param width Graph width
         * @param height Graph height
         */
        void render(LGFX_Sprite* canvas, int x, int y, int width, int height);

        /**
         * @brief Get value at a specific X position (for touch/cursor)
         * @param x_pos X position relative to graph origin
         * @param width Graph width
         * @return Value at that position, or NaN if no data
         */
        float getValueAtX(int x_pos, int width);

    private:
        std::vector<Mesh::GraphPoint> _points;
        float _min_y = 0;
        float _max_y = 100;
        bool _auto_scale = true;
        uint16_t _line_color = 0x07FF; // TFT_CYAN
        uint16_t _grid_color = 0x4208; // Dark gray
        uint32_t _bg_color = 0x333333;
        int _grid_lines = 4;
    };

} // namespace MOONCAKE::APPS

