import multiprocessing
import deckhand

multiprocessing.set_start_method("spawn", force=True)

m = deckhand.load_model("tvm:mlp")
m.tune()
