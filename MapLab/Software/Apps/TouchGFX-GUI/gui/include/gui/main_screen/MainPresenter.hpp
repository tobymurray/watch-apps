#ifndef MAINPRESENTER_HPP
#define MAINPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainView;

class MainPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPresenter(MainView &v);
    virtual ~MainPresenter() {}

    virtual void activate();
    virtual void deactivate();

    void exit() { model->exitApp(); }

    const Model::Status &status() const { return model->status(); }

    // The view drives the model through these rather than reaching for it:
    // the presenter is the only thing that knows both.
    void up()     { model->up(); }
    void down()   { model->down(); }
    void select() { model->select(); }
    void back()   { model->back(); }

    const uint8_t *canvasPixels() const { return model->canvasPixels(); }
    const SDK::Kernel &kernel() const   { return model->kernel(); }

    bool blitPending(int &index, uint32_t &repeats, const uint8_t *&source,
                     int16_t &srcW, int16_t &srcH, bool &mosaic)
    {
        return model->blitPending(index, repeats, source, srcW, srcH, mosaic);
    }
    void blitComplete(int index, uint32_t iterations, uint32_t elapsedMs,
                      int32_t bytesPerBlit)
    {
        model->blitComplete(index, iterations, elapsedMs, bytesPerBlit);
    }

    virtual void onStatusChanged(const Model::Status &status) override;

private:
    MainPresenter();
    MainView &view;
};

#endif // MAINPRESENTER_HPP
