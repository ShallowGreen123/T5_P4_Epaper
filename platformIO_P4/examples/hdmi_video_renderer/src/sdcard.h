#pragma once

class SdCard {
public:
    bool mount();
    void unmount();
    bool mounted() const;

private:
    void *card_{};
};

