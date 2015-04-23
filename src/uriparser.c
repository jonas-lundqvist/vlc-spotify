/*****************************************************************************
 * Copyright (C) 2015 Jonas Lundqvist
 *
 * Author: Jonas Lundqvist <jonas@gannon.se>
 *
 * This file is part of vlc-spotify.
 *
 * vlc-spotify is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "uriparser.h"

spotify_type_e ParseURI(const char *uri_in, char **uri_out)
{
    char         *psz_dup = strdup(uri_in);
    char         *psz_parser = psz_dup;
    char         *tmp;

    spotify_type_e spotify_type = SPOTIFY_UNKNOWN;

    *uri_out = (char *) malloc(36);
    strcpy(*uri_out, "");

    if (psz_parser == NULL) {
        free(psz_dup);
        return SPOTIFY_UNKNOWN;
    }

    if (strlen(psz_parser) < 36) {
        free(psz_dup);
        return SPOTIFY_UNKNOWN;
    }

    // Find 'spotify:' and make sure it is in the start
    tmp = strstr(psz_parser, "spotify:");
    if (tmp != psz_parser) {
        free(psz_dup);
        return SPOTIFY_UNKNOWN;
    }

    psz_parser += 8;

    if ((tmp = strstr(psz_parser, "track:")) == psz_parser) {
        spotify_type = SPOTIFY_TRACK;
        psz_parser += 6;
    } else if ((tmp = strstr(psz_parser, "album:")) == psz_parser) {
        spotify_type = SPOTIFY_ALBUM;
        psz_parser += 6;
    } else {
        spotify_type = SPOTIFY_UNKNOWN;
    }

    // Check that the id is 22 chars
    if (strlen(psz_parser) != 22) {
        spotify_type = SPOTIFY_UNKNOWN;
    } else {
        strcat(*uri_out, psz_dup);
    }

    free(psz_dup);

    return spotify_type;
}
