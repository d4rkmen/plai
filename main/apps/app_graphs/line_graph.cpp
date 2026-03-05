/**
 * @file line_graph.cpp
 * @author d4rkmen
 * @brief Reusable line graph rendering component
 * @version 1.0
 * @date 2025-01-03
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "line_graph.h"
#include <algorithm>
#include <cmath>

namespace MOONCAKE::APPS
{

    void LineGraph::setData(const std::vector<Mesh::GraphPoint>& points)
    {
        _points = points;
        
        // Auto-scale Y axis
        if (_auto_scale && !_points.empty())
        {
            _min_y = _points[0].value;
            _max_y = _points[0].value;
            
            for (const auto& pt : _points)
            {
                if (pt.value < _min_y) _min_y = pt.value;
                if (pt.value > _max_y) _max_y = pt.value;
            }
            
            // Add 10% margin
            float range = _max_y - _min_y;
            if (range < 0.001f) range = 1.0f;
            _min_y -= range * 0.1f;
            _max_y += range * 0.1f;
        }
    }

    void LineGraph::setYRange(float min_y, float max_y)
    {
        _min_y = min_y;
        _max_y = max_y;
        _auto_scale = false;
    }

    void LineGraph::render(LGFX_Sprite* canvas, int x, int y, int width, int height)
    {
        // Background
        canvas->fillRect(x, y, width, height, _bg_color);
        
        // Draw border
        canvas->drawRect(x, y, width, height, _grid_color);
        
        // Draw grid lines
        if (_grid_lines > 0)
        {
            for (int i = 1; i < _grid_lines; i++)
            {
                int gy = y + (height * i) / _grid_lines;
                canvas->drawFastHLine(x + 1, gy, width - 2, _grid_color);
            }
        }
        
        // No data case
        if (_points.empty())
        {
            return;
        }
        
        // Calculate scaling
        float y_range = _max_y - _min_y;
        if (y_range < 0.001f) y_range = 1.0f;
        
        // Find time range
        uint32_t min_time = _points.front().timestamp_ms;
        uint32_t max_time = _points.back().timestamp_ms;
        uint32_t time_range = max_time - min_time;
        if (time_range == 0) time_range = 1;
        
        // Draw line
        int prev_px = -1, prev_py = -1;
        
        for (const auto& pt : _points)
        {
            // Map time to X
            int px = x + 1 + ((pt.timestamp_ms - min_time) * (width - 2)) / time_range;
            
            // Map value to Y (inverted - higher values at top)
            float normalized = (pt.value - _min_y) / y_range;
            int py = y + height - 2 - (int)(normalized * (height - 4));
            
            // Clamp
            if (py < y + 1) py = y + 1;
            if (py > y + height - 2) py = y + height - 2;
            
            // Draw line segment
            if (prev_px >= 0)
            {
                canvas->drawLine(prev_px, prev_py, px, py, _line_color);
            }
            
            prev_px = px;
            prev_py = py;
        }
        
        // Draw points
        for (const auto& pt : _points)
        {
            int px = x + 1 + ((pt.timestamp_ms - min_time) * (width - 2)) / time_range;
            float normalized = (pt.value - _min_y) / y_range;
            int py = y + height - 2 - (int)(normalized * (height - 4));
            
            if (py < y + 1) py = y + 1;
            if (py > y + height - 2) py = y + height - 2;
            
            canvas->fillCircle(px, py, 2, _line_color);
        }
    }

    float LineGraph::getValueAtX(int x_pos, int width)
    {
        if (_points.empty() || width <= 0)
        {
            return NAN;
        }
        
        uint32_t min_time = _points.front().timestamp_ms;
        uint32_t max_time = _points.back().timestamp_ms;
        uint32_t time_range = max_time - min_time;
        if (time_range == 0) return _points[0].value;
        
        // Find target time
        uint32_t target_time = min_time + (x_pos * time_range) / width;
        
        // Find closest point
        float closest_value = _points[0].value;
        uint32_t closest_dist = UINT32_MAX;
        
        for (const auto& pt : _points)
        {
            uint32_t dist = (pt.timestamp_ms > target_time) ? 
                            (pt.timestamp_ms - target_time) : 
                            (target_time - pt.timestamp_ms);
            if (dist < closest_dist)
            {
                closest_dist = dist;
                closest_value = pt.value;
            }
        }
        
        return closest_value;
    }

} // namespace MOONCAKE::APPS

