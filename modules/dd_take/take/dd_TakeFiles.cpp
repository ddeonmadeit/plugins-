namespace dd::takefiles
{
juce::File folder (const juce::String& pluginName, const juce::String& sub)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("ddeonmadeit").getChildFile (pluginName).getChildFile (sub);
    dir.createDirectory();
    return dir;
}

juce::File newTakeFile (const juce::String& pluginName, const juce::String& extension)
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto id = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()).substring (0, 8);
    return folder (pluginName).getChildFile (stamp + "-" + id + extension);
}

bool save (const TakeStore& store, Span span, double sampleRate, const juce::File& file)
{
    if (span.length() <= 0)
        return false;
    file.getParentDirectory().createDirectory();
    const auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".part");
    temp.deleteFile();
    {
        auto stream = temp.createOutputStream();
        if (stream == nullptr)
            return false;
        std::unique_ptr<juce::OutputStream> os (stream.release());
        juce::FlacAudioFormat flac;
        auto writer = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                  .withNumChannels (1)
                                                                                  .withBitsPerSample (24));
        if (writer == nullptr)
            return false;
        constexpr int kBlock = 16384;
        juce::AudioBuffer<float> buf (1, kBlock);
        for (int64_t p = span.start; p < span.end; p += kBlock)
        {
            const int n = (int) std::min<int64_t> (kBlock, span.end - p);
            store.read (p, buf.getWritePointer (0), n);
            for (int i = 0; i < n; ++i)
                buf.getWritePointer (0)[i] = juce::jlimit (-1.0f, 1.0f, buf.getReadPointer (0)[i]);
            if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
                return false;
        }
    }
    file.deleteFile();
    return temp.moveFileTo (file);
}

bool writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::OutputStream> os (stream.release());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                            .withNumChannels (buffer.getNumChannels())
                                                                            .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

bool canLoad (const juce::File& file)
{
    return file.hasFileExtension (".wav;.wave;.aif;.aiff;.flac;.ogg;.mp3;.m4a;.caf");
}

Loaded load (TakeStore& store, int64_t at, const juce::File& file, double targetRate, const std::atomic<bool>* cancel)
{
    Loaded result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        result.error = "Can't read " + file.getFileName();
        return result;
    }
    result.fileRate = reader->sampleRate;
    const int channels = (int) std::max (1u, reader->numChannels);
    const auto length = (int64_t) reader->lengthInSamples;
    if (length <= 0 || length > TakeStore::capacity())
    {
        result.error = file.getFileName() + " is empty or too long";
        return result;
    }

    // The whole file as mono first (a few minutes of vocal is tens of MB), so the rate conversion below
    // runs as one continuous stream and stays sample-accurate against the timeline.
    std::vector<float> mono ((size_t) length);
    constexpr int kBlock = 32768;
    juce::AudioBuffer<float> in (channels, kBlock);
    for (int64_t p = 0; p < length; p += kBlock)
    {
        if (cancel != nullptr && cancel->load())
            return result;
        const int n = (int) std::min<int64_t> (kBlock, length - p);
        reader->read (&in, 0, n, p, true, true);
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                sum += in.getReadPointer (ch)[i];
            mono[(size_t) (p + i)] = sum / (float) channels;
        }
    }

    int64_t written = 0;
    const double ratio = reader->sampleRate / targetRate;   // input samples per output sample
    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        store.write (at, mono.data(), (int) length);
        written = length;
    }
    else
    {
        juce::WindowedSincInterpolator interp;
        std::vector<float> out ((size_t) kBlock);
        const auto total = (int64_t) std::floor ((double) length / ratio);
        int64_t used = 0;
        while (written < total && used < length)
        {
            if (cancel != nullptr && cancel->load())
                break;
            const int n = (int) std::min<int64_t> (kBlock, total - written);
            used += interp.process (ratio, mono.data() + used, out.data(), n, (int) (length - used), 0);
            store.write (at + written, out.data(), n);
            written += n;
        }
    }
    result.ok = written > 0;
    result.length = written;
    if (! result.ok)
        result.error = file.getFileName() + " is empty";
    return result;
}
} // namespace dd::takefiles
