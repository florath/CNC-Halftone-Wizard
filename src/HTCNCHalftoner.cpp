/******************************************************************************
* CNC Halftone Wizard
* Copyright (C) 2011 Paul Kerchen
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "HTCNCHalftoner.h"

#include <QImage>
#include <QPixmap>
#include <QPainter>

#include <cmath>
#include <iostream>

namespace HTCNC {
// Returns the dot size for a given point in an image.  The returned value
// is in the range [0..1].  The dot size is determined by averaging the
// greyscale intensity of each pixel within a square boundary around the point.
// radius is the number of pixels to consider around (x,y).  For example,
// a radius of 2 would mean that all pixels in the range [(x-2, y-2)..(x+1,
// y+1)]
// would be averaged to determine the final dot size.  Note that the
// radius determines a square (not a circle) around the point that is
// being queried.
double getDotSize(QImage const &src, int const x, int const y,
                  int const radius) {
  int total_intensity(0);
  int pix_count(0);

  for (int x_idx(x - radius); x_idx < x + radius; ++x_idx) {
    for (int y_idx(y - radius); y_idx < y + radius; ++y_idx) {
      // Bounds checking.
      if (x_idx < 0 or x_idx >= src.width() or y_idx < 0 or
          y_idx >= src.height()) {
        continue;
      }
      total_intensity += qGray(src.pixel(x_idx, y_idx));
      ++pix_count;
    }
  }
  return static_cast<double>(total_intensity) / pix_count / 255.0;
}

Halftoner::Halftoner(QPixmap const &src, QImage &dest, int const scale,
                     bool const generateGCode, CNCParameters const &params)
    : m_cutCount(0) {
  double const max_dot_size(params.m_fullToolWidth * params.m_maxCutPercent);

  QImage const src_img(src.toImage());

  //  dest.fill(qRgb(0, 0, 0));

  //  Init the painter
  QPainter p;
  p.begin(&dest);

  // DestinationOver results in the current painting
  // going below the existing image.
  p.setRenderHints(QPainter::HighQualityAntialiasing);

  p.setBrush(Qt::black);
  p.setPen(QPen(Qt::white, 3.0));

  // Erase the whole image.
  p.drawRect(0, 0, dest.width(), dest.height());

  p.setBrush(Qt::white);

  int offset(0);
  bool write_y(true);

  for (double y(params.m_dotDistance * scale);
       y < params.m_targetHeight * scale - params.m_dotDistance * scale;
       y += params.m_dotDistance * scale) {
    write_y = true;
    int const y_src(y * src_img.height() / (params.m_targetHeight * scale));
    for (double x(params.m_dotDistance * scale + offset);
         x < params.m_targetWidth * scale - params.m_dotDistance * scale -
                 offset - 1.0; // -1 => rounding.
         x += params.m_dotDistance * scale) {
      int const x_src(x * src_img.width() / (params.m_targetWidth * scale));

      double const ds(getDotSize(src_img, x_src, y_src, params.m_step));
      if (ds == 0.0) {
        continue;
      }
      ++m_cutCount;
      p.drawEllipse(x, y, ds * max_dot_size * scale, ds * max_dot_size * scale);

      // ToDo: refactor: extract method.
      if (generateGCode) {
        // Write the g code to cut this dot.
        // Lift tool to safe 'fast z' depth.
        m_gCode += "G00Z" + QString::number(params.m_fastZ) + "\n";
        m_gCode += "G00X" + QString::number(x / scale);
        if (write_y) {
          m_gCode += "Y" + QString::number(y / scale);
          write_y = false;
        }
        m_gCode += "\n";

        // Move tool to cut depth.
        m_gCode += "G01Z" + QString::number(-params.m_fullToolDepth *
                                            params.m_maxCutPercent * ds) +
                   "\n";
      }
    }
    if (offset) {
      offset = 0;
    } else {
      offset = params.m_dotDistance / 2 * scale;
    }
  }

  p.end();

  // Finally, make sure the tool is parked at a safe depth.
  m_gCode += "G00Z" + QString::number(
                          params.m_fastZ); // Lift tool to safe 'fast z' depth.
  m_gCode += "\n";
}

#if 0
Halftoner::Halftoner(QPixmap const &src, QImage &dest, int const scale,
                     bool const generateGCode, CNCParameters const &params)
    : m_cutCount(0) {
  QImage const src_img(src.toImage());
  int offset = params.m_step;
  int const radius = params.m_step;
  double const max_dot_size(params.m_fullToolWidth * params.m_maxCutPercent);
  double const scale_factor(scale);
  bool write_y(true);

  dest.fill(qRgb(0, 0, 0));

  // Basic approach: Step through the source image and convert each
  // point to a circle in the destination image and a tool cut in the
  // g code.  Every other row is offset by half a step to achieve the
  // zig-zag pattern of a typical halftone image.
  for (int y = params.m_step / 2, cy = src_img.height() / params.m_step;
       y < src_img.height(); y += params.m_step, --cy, write_y = true) {
    for (int x = offset, cx = 1; x < src_img.width();
         x += params.m_step, ++cx) {
      double ds(getDotSize(src_img, x, y, radius));

      // Simple optimization: if the dot size is zero, just fill the destination
      // area with black pixels and don't generate any g code.
      if (ds == 0) {
        for (int i = scale_factor * (x - radius);
             i < scale_factor * (x + radius); ++i) {
          for (int j = scale_factor * (y - radius);
               j < scale_factor * (y + radius); ++j) {
            if (i >= 0 && i < dest.width() && j >= 0 && j < dest.height()) {
              dest.setPixel(i, j, qRgb(0, 0, 0));
            }
          }
        }
      } else {
        // Draw a circle and generate some tool movement g code.

        ++m_cutCount;

        if (generateGCode) {
          // Write the g code to cut this dot.
          // Lift tool to safe 'fast z' depth.
          m_gCode += "G00Z" + QString::number(params.m_fastZ) + "\n";

          // Move tool to cut location.
          double cut_x(cx * (max_dot_size + params.m_minDotGap));

          if (offset)
            cut_x -= max_dot_size / 2.0;
          m_gCode += "G00X" + QString::number(cut_x);
          if (write_y) {
            double cut_y(cy * (max_dot_size + params.m_minDotGap));
            m_gCode += "Y" + QString::number(cut_y);
            write_y = false;
          }
          m_gCode += "\n";

          // Move tool to cut depth.
          m_gCode += "G01Z" + QString::number(-params.m_fullToolDepth *
                                              params.m_maxCutPercent * ds) +
                     "\n";
        }

        // Draw a circle in the preview image.
        double ds2(radius * radius * ds * ds * scale_factor * scale_factor);
        for (int i = scale_factor * (x - radius);
             i < scale_factor * (x + radius); ++i) {
          for (int j = scale_factor * (y - radius);
               j < scale_factor * (y + radius); ++j) {
            if (i >= 0 && i < dest.width() && j >= 0 && j < dest.height()) {
              int dx(i - scale_factor * x), dy(j - scale_factor * y);

              if (dx * dx + dy * dy < ds2 - 0.5)
                dest.setPixel(i, j, qRgb(255, 255, 255));
              // Make the border pixels grey to improve the appearance a bit.
              else if (dx * dx + dy * dy < ds2 + 0.5)
                dest.setPixel(i, j, qRgb(127, 127, 127));
              else {
                dest.setPixel(i, j, qRgb(0, 0, 0));
              }
            }
          }
        }
      }
    }
    // Zig-zag in x.
    if (offset)
      offset = 0;
    else
      offset = params.m_step / 2;
  }
  // Finally, make sure the tool is parked at a safe depth.
  m_gCode += "G00Z" + QString::number(
                          params.m_fastZ); // Lift tool to safe 'fast z' depth.
  m_gCode += "\n";
}
#endif
}
