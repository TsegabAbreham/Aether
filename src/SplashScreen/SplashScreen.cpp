
#include "SplashScreen.h"
#include <cmath>

namespace
{
    // Draw an image while preserving its aspect ratio.
    // The image fills the destination area, cropping excess edges.
    void drawImageCover(
        juce::Graphics& g,
        const juce::Image& image,
        juce::Rectangle<float> destination)
    {
        if (!image.isValid() || destination.isEmpty())
            return;

        const float imageWidth = static_cast<float>(image.getWidth());
        const float imageHeight = static_cast<float>(image.getHeight());

        const float destinationWidth = destination.getWidth();
        const float destinationHeight = destination.getHeight();

        const float imageRatio = imageWidth / imageHeight;
        const float destinationRatio = destinationWidth / destinationHeight;

        float sourceX = 0.0f;
        float sourceY = 0.0f;
        float sourceWidth = imageWidth;
        float sourceHeight = imageHeight;

        // Crop horizontally if the image is wider than the destination.
        if (imageRatio > destinationRatio)
        {
            sourceWidth = imageHeight * destinationRatio;
            sourceX = (imageWidth - sourceWidth) * 0.5f;
        }
        // Crop vertically if the image is taller than the destination.
        else if (imageRatio < destinationRatio)
        {
            sourceHeight = imageWidth / destinationRatio;
            sourceY = (imageHeight - sourceHeight) * 0.5f;
        }

        g.drawImage(
            image,
            juce::roundToInt(destination.getX()),
            juce::roundToInt(destination.getY()),
            juce::roundToInt(destinationWidth),
            juce::roundToInt(destinationHeight),
            juce::roundToInt(sourceX),
            juce::roundToInt(sourceY),
            juce::roundToInt(sourceWidth),
            juce::roundToInt(sourceHeight)
        );
    }

    // Draw an image entirely inside the destination area.
    // Nothing gets cropped or distorted.
    void drawImageContain(
        juce::Graphics& g,
        const juce::Image& image,
        juce::Rectangle<float> destination)
    {
        if (!image.isValid() || destination.isEmpty())
            return;

        const float imageRatio =
            static_cast<float>(image.getWidth()) /
            static_cast<float>(image.getHeight());

        const float destinationRatio =
            destination.getWidth() / destination.getHeight();

        float width = destination.getWidth();
        float height = destination.getHeight();

        if (imageRatio > destinationRatio)
            height = width / imageRatio;
        else
            width = height * imageRatio;

        const auto fitted = juce::Rectangle<float>(
            destination.getCentreX() - width * 0.5f,
            destination.getCentreY() - height * 0.5f,
            width,
            height
        );

        g.drawImage(
            image,
            juce::roundToInt(fitted.getX()),
            juce::roundToInt(fitted.getY()),
            juce::roundToInt(fitted.getWidth()),
            juce::roundToInt(fitted.getHeight()),
            0,
            0,
            image.getWidth(),
            image.getHeight()
        );
    }
}

AetherSplashScreen::AetherSplashScreen()
{
    setSize(1000, 562);

    // Load images relative to the executable.
    const auto executableDir =
        juce::File::getSpecialLocation(
            juce::File::currentExecutableFile
        ).getParentDirectory();

    backgroundImage = juce::ImageFileFormat::loadFrom(
        executableDir.getChildFile("assets/icon/Aether_Splashscreen.png")
    );

    // Generate subtle background particles.
    constexpr int particleCount = 90;

    particles.reserve(particleCount);

    for (int i = 0; i < particleCount; ++i)
    {
        Particle p;

        p.x = rng.nextFloat();
        p.y = rng.nextFloat();
        p.z = rng.nextFloat();
        p.speed = 0.05f + rng.nextFloat() * 0.2f;
        p.twinkle = rng.nextFloat() * juce::MathConstants<float>::twoPi;

        particles.push_back(p);
    }

    startTimerHz(30);
}

void AetherSplashScreen::setProgress(
    double newProgress,
    const juce::String& statusText)
{
    targetProgress = juce::jlimit(0.0, 1.0, newProgress);
    status = statusText;
    repaint();
}

void AetherSplashScreen::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    paintBackground(g, bounds);

    // Dark overlay to keep text readable.
    g.setColour(juce::Colour(0x44000000));
    g.fillRect(bounds);

    paintRipples(g, bounds.getCentre());

    // Subtle foreground particles.
    for (const auto& p : particles)
    {
        const float x = p.x * bounds.getWidth();
        const float y = p.y * bounds.getHeight();

        const float alpha =
            0.12f + 0.18f *
            (0.5f + 0.5f * std::sin(phase + p.twinkle));

        const float radius = 0.7f + p.z * 1.2f;

        g.setColour(
            juce::Colours::lightcyan.withAlpha(alpha)
        );

        g.fillEllipse(
            x - radius,
            y - radius,
            radius * 2.0f,
            radius * 2.0f
        );
    }


    // Progress bar.
    const float barWidth = bounds.getWidth() * 0.36f;
    const float barHeight = 3.0f;

    const juce::Rectangle<float> progressBar(
        bounds.getCentreX() - barWidth * 0.5f,
        bounds.getHeight() * 0.88f,
        barWidth,
        barHeight
    );

    paintProgress(g, progressBar);

    // Loading status.
    g.setColour(juce::Colour(0xff91aab9));

    juce::Font statusFont(
        juce::Font::getDefaultSansSerifFontName(),
        11.0f,
        juce::Font::plain
    );

    g.setFont(statusFont);

    g.drawFittedText(
        status,
        0,
        juce::roundToInt(progressBar.getBottom() + 12.0f),
        getWidth(),
        20,
        juce::Justification::centred,
        1
    );

    // Thin border.
    g.setColour(juce::Colour(0x4479b9cf));
    g.drawRect(bounds, 1.0f);
}

void AetherSplashScreen::paintBackground(
    juce::Graphics& g,
    juce::Rectangle<float> bounds)
{
    if (backgroundImage.isValid())
    {
        // IMPORTANT:
        // Preserve the original image ratio instead of stretching it.
        drawImageCover(g, backgroundImage, bounds);
    }
    else
    {
        // Fallback if the background image cannot be loaded.
        juce::ColourGradient gradient(
            juce::Colour(0xff101a27),
            bounds.getTopLeft(),
            juce::Colour(0xff03070d),
            bounds.getBottomRight(),
            false
        );

        g.setGradientFill(gradient);
        g.fillRect(bounds);
    }
}

void AetherSplashScreen::paintRipples(
    juce::Graphics& g,
    juce::Point<float> centre)
{
    const float baseRadius = getHeight() * 0.24f;

    for (int i = 0; i < 4; ++i)
    {
        const float radius =
            baseRadius + static_cast<float>(i) * 24.0f;

        const float alpha =
            0.10f - static_cast<float>(i) * 0.018f;

        g.setColour(
            juce::Colour(0xff69c7e3).withAlpha(alpha)
        );

        g.drawEllipse(
            centre.x - radius,
            centre.y - radius,
            radius * 2.0f,
            radius * 2.0f,
            0.8f
        );
    }
}

void AetherSplashScreen::paintWaveform(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    float y)
{
    juce::Path waveform;

    constexpr int samples = 120;

    for (int i = 0; i <= samples; ++i)
    {
        const float t = static_cast<float>(i) / samples;

        const float x = area.getX() + t * area.getWidth();

        const float envelope =
            std::sin(t * juce::MathConstants<float>::pi);

        const float wave =
            std::sin(t * 38.0f + phase * 0.8f) * 5.0f +
            std::sin(t * 73.0f - phase) * 2.0f;

        const float currentY = y + wave * envelope;

        if (i == 0)
            waveform.startNewSubPath(x, currentY);
        else
            waveform.lineTo(x, currentY);
    }

    g.setColour(juce::Colour(0x8871cce8));
    g.strokePath(
        waveform,
        juce::PathStrokeType(1.2f)
    );
}

void AetherSplashScreen::paintProgress(
    juce::Graphics& g,
    juce::Rectangle<float> bar)
{
    // Background track.
    g.setColour(juce::Colour(0x557f9bad));
    g.fillRoundedRectangle(bar, 2.0f);

    // Progress fill.
    auto filled = bar.withWidth(
        bar.getWidth() * static_cast<float>(shownProgress)
    );

    if (filled.getWidth() > 0.0f)
    {
        juce::ColourGradient gradient(
            juce::Colour(0xff4c9fbe),
            filled.getTopLeft(),
            juce::Colour(0xffa1e6f4),
            filled.getTopRight(),
            false
        );

        g.setGradientFill(gradient);
        g.fillRoundedRectangle(filled, 2.0f);
    }
}

void AetherSplashScreen::timerCallback()
{
    const double now =
        juce::Time::getMillisecondCounterHiRes();

    if (lastTickMs == 0.0)
        lastTickMs = now;

    const float deltaTime = static_cast<float>(
        (now - lastTickMs) / 1000.0
    );

    lastTickMs = now;

    phase += deltaTime * 0.8f;

    if (phase > juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;

    // Smoothly animate the progress bar.
    shownProgress +=
        (targetProgress - shownProgress) *
        juce::jlimit(0.0, 1.0, deltaTime * 5.0);

    if (std::abs(targetProgress - shownProgress) < 0.001)
        shownProgress = targetProgress;

    // Move the particles very subtly.
    for (auto& p : particles)
    {
        p.y -= p.speed * deltaTime * 0.025f;

        if (p.y < 0.0f)
        {
            p.y = 1.0f;
            p.x = rng.nextFloat();
        }
    }

    // Fire completion callback once the progress reaches 100%.
    if (shownProgress >= 0.999 && targetProgress >= 1.0)
    {
        if (finishedSince < 0.0)
            finishedSince = now;

        if (!finishedFired && now - finishedSince >= 400.0)
        {
            finishedFired = true;

            stopTimer();

            if (onFinished)
                onFinished();
        }
    }
    else
    {
        finishedSince = -1.0;
    }

    repaint();
}
