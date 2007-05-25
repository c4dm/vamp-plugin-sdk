/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Vamp

    An API for audio analysis and feature extraction plugins.

    Centre for Digital Music, Queen Mary, University of London.
    Copyright 2006 Chris Cannam.
  
    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the names of the Centre for
    Digital Music; Queen Mary, University of London; and Chris Cannam
    shall not be used in advertising or otherwise to promote the sale,
    use or other dealings in this Software without prior written
    authorization.
*/

#include "PluginInputDomainAdapter.h"

#include <cmath>

namespace Vamp {

namespace HostExt {

PluginInputDomainAdapter::PluginInputDomainAdapter(Plugin *plugin) :
    PluginWrapper(plugin),
    m_channels(0),
    m_blockSize(0),
    m_freqbuf(0)
{
}

PluginInputDomainAdapter::~PluginInputDomainAdapter()
{
}
    
bool
PluginInputDomainAdapter::initialise(size_t channels, size_t stepSize, size_t blockSize)
{
    //!!! complain and adapt-or-die if blocksize is not a power of 2

    if (m_plugin->getInputDomain() == FrequencyDomain) {
        if (m_channels > 0) {
            for (size_t c = 0; c < m_channels; ++c) {
                delete[] m_freqbuf[c];
            }
            delete[] m_freqbuf;
            delete[] m_ri;
            delete[] m_ro;
            delete[] m_io;
        }
    }

    m_channels = channels;
    m_blockSize = blockSize;

    if (m_plugin->getInputDomain() == FrequencyDomain) {
        m_freqbuf = new float *[m_channels];
        for (size_t c = 0; c < m_channels; ++c) {
            m_freqbuf[c] = new float[m_blockSize + 2];
        }
        m_ri = new double[m_blockSize];
        m_ro = new double[m_blockSize];
        m_io = new double[m_blockSize];
    }

    return m_plugin->initialise(channels, stepSize, blockSize);
}

Plugin::InputDomain
PluginInputDomainAdapter::getInputDomain() const
{
    return TimeDomain;
}

size_t
PluginInputDomainAdapter::getPreferredStepSize() const
{
    size_t step = m_plugin->getPreferredStepSize();

    if (step == 0 && (m_plugin->getInputDomain() == FrequencyDomain)) {
        step = getPreferredBlockSize() / 2;
    }

    return step;
}

size_t
PluginInputDomainAdapter::getPreferredBlockSize() const
{
    //!!! complain and adapt-or-die if blocksize is not a power of 2

    size_t block = m_plugin->getPreferredBlockSize();

    if (block == 0 && (m_plugin->getInputDomain() == FrequencyDomain)) {
        block = 1024;
    }

    return block;
}

Plugin::FeatureSet
PluginInputDomainAdapter::process(const float *const *inputBuffers, RealTime timestamp)
{
    if (m_plugin->getInputDomain() == TimeDomain) {
        return m_plugin->process(inputBuffers, timestamp);
    }

    //!!! need to compensate for the fact that the first block is aligned
    // with the zero frame but for frequency domain we want it to be
    // centred on the zero frame 

    for (size_t c = 0; c < m_channels; ++c) {

        for (size_t i = 0; i < m_blockSize; ++i) {
            // Hanning window
            m_ri[i] = double(inputBuffers[c][i])
                * (0.50 - 0.50 * cos((2 * M_PI * i)
                                     / m_blockSize));
        }

        for (size_t i = 0; i < m_blockSize/2; ++i) {
            // FFT shift
            double value = m_ri[i];
            m_ri[i] = m_ri[i + m_blockSize/2];
            m_ri[i + m_blockSize/2] = value;
        }

        fft(m_blockSize, false, m_ri, 0, m_ro, m_io);

        for (size_t i = 0; i < m_blockSize/2; ++i) {
            m_freqbuf[c][i * 2] = m_ro[i];
            m_freqbuf[c][i * 2 + 1] = m_io[i];
        }
    }

    //!!! do we want to adjust the timestamp or anything so as to
    // effectively centre the frame?

    return m_plugin->process(m_freqbuf, timestamp);
}

void
PluginInputDomainAdapter::fft(unsigned int n, bool inverse,
                              double *ri, double *ii, double *ro, double *io)
{
    if (!ri || !ro || !io) return;

    unsigned int bits;
    unsigned int i, j, k, m;
    unsigned int blockSize, blockEnd;

    double tr, ti;

    if (n < 2) return;
    if (n & (n-1)) return;

    double angle = 2.0 * M_PI;
    if (inverse) angle = -angle;

    for (i = 0; ; ++i) {
	if (n & (1 << i)) {
	    bits = i;
	    break;
	}
    }

    static unsigned int tableSize = 0;
    static int *table = 0;

    if (tableSize != n) {

	delete[] table;

	table = new int[n];

	for (i = 0; i < n; ++i) {
	
	    m = i;

	    for (j = k = 0; j < bits; ++j) {
		k = (k << 1) | (m & 1);
		m >>= 1;
	    }

	    table[i] = k;
	}

	tableSize = n;
    }

    if (ii) {
	for (i = 0; i < n; ++i) {
	    ro[table[i]] = ri[i];
	    io[table[i]] = ii[i];
	}
    } else {
	for (i = 0; i < n; ++i) {
	    ro[table[i]] = ri[i];
	    io[table[i]] = 0.0;
	}
    }

    blockEnd = 1;

    for (blockSize = 2; blockSize <= n; blockSize <<= 1) {

	double delta = angle / (double)blockSize;
	double sm2 = -sin(-2 * delta);
	double sm1 = -sin(-delta);
	double cm2 = cos(-2 * delta);
	double cm1 = cos(-delta);
	double w = 2 * cm1;
	double ar[3], ai[3];

	for (i = 0; i < n; i += blockSize) {

	    ar[2] = cm2;
	    ar[1] = cm1;

	    ai[2] = sm2;
	    ai[1] = sm1;

	    for (j = i, m = 0; m < blockEnd; j++, m++) {

		ar[0] = w * ar[1] - ar[2];
		ar[2] = ar[1];
		ar[1] = ar[0];

		ai[0] = w * ai[1] - ai[2];
		ai[2] = ai[1];
		ai[1] = ai[0];

		k = j + blockEnd;
		tr = ar[0] * ro[k] - ai[0] * io[k];
		ti = ar[0] * io[k] + ai[0] * ro[k];

		ro[k] = ro[j] - tr;
		io[k] = io[j] - ti;

		ro[j] += tr;
		io[j] += ti;
	    }
	}

	blockEnd = blockSize;
    }

    if (inverse) {

	double denom = (double)n;

	for (i = 0; i < n; i++) {
	    ro[i] /= denom;
	    io[i] /= denom;
	}
    }
}

}
        
}

