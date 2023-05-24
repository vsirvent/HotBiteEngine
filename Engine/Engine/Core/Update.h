#pragma once

#include <Defines.h>

namespace HotBite {
    namespace Engine {
        namespace Core {
#if 0
            class IUpdate {
            public:
                //Update evet triggered
                virtual void Update(int64_t elapsed_nsec, int64_t total_nsec) = 0;
                //Mark to update
                virtual void UpdateMark() = 0;
            };

            class UpdateTriggerer {
            private:
                //mutable as we send signals that modify other classes,
                //but not this class.
                mutable std::vector<IUpdate*> updatables;
            public:
                //Trigger update event
                void TriggerUpdate(int64_t elapsed_nsec, int64_t total_nsec) const {
                    for (int i = 0; i < updatables.size(); ++i) {
                        updatables[i]->Update(elapsed_nsec, total_nsec);
                    }
                }
                //Set update marks
                void SetUpdateMark() const {
                    for (int i = 0; i < updatables.size(); ++i) {
                        updatables[i]->UpdateMark();
                    }
                }

            public:
                //Add as updatable
                void AddUpdatable(IUpdate* updatable) {
                    if (std::find(updatables.begin(), updatables.end(), updatable) == updatables.end()) {
                        updatables.push_back(updatable);
                    }
                }
                //Remove as updatable
                void RemoveUpdatable(IUpdate* updatable) {
                    auto it = std::find(updatables.begin(), updatables.end(), updatable);
                    if (it != updatables.end()) {
                        updatables.erase(it);
                    }
                }
            };
#endif
        }
    }
}