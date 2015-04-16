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

#include <stdio.h>
#include <stdlib.h>

#include "uriparser.h"

const char *test_vector[] = {
    "spotify:track:6wNTqBF2Y69KG9EPyj9YJD",
    "spotify:album:7GTYvV0u1AqBc8djyZdhuv",
    "track:6wNTqBF2Y69KG9EPyj9YJD",          // Missing 'spotify:'
    "album:7GTYvV0u1AqBc8djyZdhuv",          // Missing 'spotify:'
    "",                                      // Empty string
    "spotify:track:6wNTqBF2Y69KG9EPyj9YJD1", // 1 char to many
    "spotify:track:6wNTqBF2Y69KG9EPyj9YJ"    // 1 char to few
};

const spotify_type_e test_result[] = {
    SPOTIFY_TRACK,
    SPOTIFY_ALBUM,
    SPOTIFY_UNKNOWN,
    SPOTIFY_UNKNOWN,
    SPOTIFY_UNKNOWN,
    SPOTIFY_UNKNOWN,
    SPOTIFY_UNKNOWN
};

int main(int argc, char *argv[]) {
    int num_tests = sizeof(test_result) / sizeof(spotify_type_e);
    int i;
    spotify_type_e result;
    int total = 0;

    for(i = 0; i < num_tests; i++) {
        result = ParseURI(test_vector[i], NULL);
        printf("[#%d] %s: %s\n", i, test_vector[i], result == test_result[i] ? "PASS":"FAIL");
        if (result == test_result[i]) {
            total++;
        }
    }

    if (total == num_tests) {
        printf("All PASS %d/%d\n", total, total);
        return EXIT_SUCCESS;
    } else {
        printf("%d of %d PASSED\n", total, num_tests);
        return EXIT_FAILURE;
    }
}
