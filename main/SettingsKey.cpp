/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */
/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "SettingsKey.h"

namespace SettingsKey
{

QString
encoded(QString name)
{
    name.replace("%", "%25");
    name.replace("/", "%2F");
    name.replace("\\", "%5C");
    name.replace("|", "%7C");
    return name;
}

QString
decoded(QString name)
{
    name.replace("%7C", "|");
    name.replace("%5C", "\\");
    name.replace("%2F", "/");
    name.replace("%25", "%");
    return name;
}

}
