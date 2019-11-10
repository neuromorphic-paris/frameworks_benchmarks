/*
 * Copyright (C) 2006-2019 Istituto Italiano di Tecnologia (IIT)
 * Copyright (C) 2006-2010 RobotCub Consortium
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the
 * BSD-3-Clause license. See the accompanying LICENSE file for details.
 */

#ifndef YARP_OS_PORTABLE_H
#define YARP_OS_PORTABLE_H

#include <yarp/os/api.h>

#include <yarp/os/PortReader.h>
#include <yarp/os/PortWriter.h>

namespace yarp {
namespace os {

/**
 * \ingroup comm_class
 *
 * This is a base class for objects that can be both read from
 * and be written to the YARP network.  It is a simple union of
 * PortReader and PortWriter.
 */
class YARP_OS_API Portable : public PortReader, public PortWriter
{
public:
    // reiterate the key inherited virtual methods, just as a reminder

    bool read(ConnectionReader& reader) override = 0;
    bool write(ConnectionWriter& writer) const override = 0;

    virtual Type getType() const;

    /**
     * Copy one portable to another, via writing and reading.
     *
     * @return true iff writer.write and reader.read both succeeded.
     */
    static bool copyPortable(PortWriter& writer, PortReader& reader);
};

} // namespace os
} // namespace yarp

#endif // YARP_OS_PORTABLE_H