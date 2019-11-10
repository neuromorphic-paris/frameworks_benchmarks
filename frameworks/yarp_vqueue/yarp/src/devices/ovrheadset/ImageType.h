/*
 * Copyright (C) 2006-2019 Istituto Italiano di Tecnologia (IIT)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef YARP_OVRHEADSET_IMAGETYPE_H
#define YARP_OVRHEADSET_IMAGETYPE_H

namespace yarp { namespace sig { template <typename T> class ImageOf; }}
namespace yarp { namespace sig { struct PixelRgb; }}
typedef yarp::sig::ImageOf<yarp::sig::PixelRgb> ImageType;

#endif // YARP_OVRHEADSET_IMAGETYPE_H