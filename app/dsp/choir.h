/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Tomorrow's Man (SAMSARA) */
/* The choir pad voice's three single cycles as harmonic amplitudes (harmonics 1 to 32, each a sine in phase):
 * amb.c builds its tables from them at start. Made by the kit's tools/choir_harmonics.py from Adventure Kid
 * Waveforms (AKWF, Kristoffer Ekstrand; CC0 1.0, a public-domain dedication): AKWF_hvoice_0024, _0076 and
 * _0052, levelled to an RMS of 0.7071. From the plainest to the fullest. */
#define CHOIR_NCYC 3
#define CHOIR_NH 32
static const float CHOIR_H[CHOIR_NCYC][CHOIR_NH] = {
    {   /* AKWF_hvoice_0024 */
        0.7280781f, 0.6778478f, 0.0559279f, 0.0287691f, 0.0109639f, 0.0087460f,
        0.0076562f, 0.0070896f, 0.0066136f, 0.0074636f, 0.0068051f, 0.0135487f,
        0.0618982f, 0.0327187f, 0.0144089f, 0.0169792f, 0.0144213f, 0.0095263f,
        0.0061042f, 0.0061953f, 0.0033636f, 0.0026882f, 0.0020835f, 0.0019477f,
        0.0030780f, 0.0023215f, 0.0015462f, 0.0011573f, 0.0015199f, 0.0016815f,
        0.0030702f, 0.0014579f,
    },
    {   /* AKWF_hvoice_0076 */
        0.7632389f, 0.5679846f, 0.2117818f, 0.2051661f, 0.0792658f, 0.0347066f,
        0.0118896f, 0.0105760f, 0.0078796f, 0.0027072f, 0.0047803f, 0.0032416f,
        0.0014696f, 0.0033236f, 0.0056480f, 0.0021422f, 0.0012609f, 0.0015209f,
        0.0013565f, 0.0003756f, 0.0009845f, 0.0000069f, 0.0009769f, 0.0010438f,
        0.0004158f, 0.0021787f, 0.0022059f, 0.0015512f, 0.0005732f, 0.0005426f,
        0.0004254f, 0.0001886f,
    },
    {   /* AKWF_hvoice_0052 */
        0.7123716f, 0.6172971f, 0.2315117f, 0.1292836f, 0.0792492f, 0.0500527f,
        0.0312138f, 0.0371331f, 0.0428833f, 0.0481231f, 0.0371504f, 0.0377267f,
        0.0445516f, 0.0464762f, 0.0434918f, 0.0491026f, 0.0387094f, 0.0399449f,
        0.0449455f, 0.0398608f, 0.0303517f, 0.0233072f, 0.0289644f, 0.0250524f,
        0.0257423f, 0.0257379f, 0.0272827f, 0.0242311f, 0.0218898f, 0.0223641f,
        0.0245755f, 0.0272145f,
    },
};
