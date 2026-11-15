// The whole program of the `link_<layer>` targets (CMakeLists.txt): one component layer, every object forced in, linked against only the layers
// below it. That it links at all is the proof that the layer calls nothing above itself; there is nothing to run.
int main()
{
    return 0;
}
