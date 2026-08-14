#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>

class ModelListener
{
public:
    ModelListener() : model(0) {}

    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }

    /**
     * @brief The service published a new progress snapshot.
     * @param progress The new snapshot.
     */
    virtual void onProgressChanged(const Model::Progress &progress) { (void)progress; }

    /**
     * @brief The service published a complete roster.
     * @param roster Every pack it tracks, and where each has got to.
     */
    virtual void onRosterChanged(const Model::Roster &roster) { (void)roster; }

protected:
    Model* model;
};

#endif // MODELLISTENER_HPP
