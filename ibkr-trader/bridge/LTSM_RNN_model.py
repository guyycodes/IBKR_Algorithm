# # ────────────────────────────────────────────────────────────────
# #  trading_lstm.py  –  end-to-end LSTM forecaster for tick data
# #  * Python 3.11  * PyTorch ≥ 2.2  * macOS / Linux / Windows
# # ────────────────────────────────────────────────────────────────
# from __future__ import annotations

# import dataclasses, math, time, json, pathlib, warnings
# from typing import Any, Dict, List, Optional, Sequence, Tuple

# import torch, torch.nn.functional as F
# from torch import nn
# from torch.utils.data import Dataset, DataLoader
# from torch.cuda.amp import autocast, GradScaler

# # ---------- 1.  Configuration -------------------------------------------------

# @dataclasses.dataclass(slots=True)
# class TradingLSTMConfig:
#     hidden_size:          int  = 256
#     num_lstm_layers:      int  = 3
#     input_feature_size:   int  = 20          # filled in at runtime
#     dropout:              float= 0.2
#     bidirectional:        bool = False

#     sequence_length:      int  = 50
#     prediction_horizon:   int  = 5

#     num_classes:          int  = 5           # Strong-Sell … Strong-Buy
#     classification:       bool = True

#     # LoRA
#     lora_rank_momentum:       int = 32
#     lora_rank_mean_revert:    int = 32
#     lora_rank_scalping:       int = 32
#     lora_scale:               float = 1.0

#     # Feature flags
#     use_technical_indicators: bool = True
#     use_volume_features:      bool = True
#     normalize_features:       bool = True

#     # Time & session encoding
#     use_time_encoding:        bool = True
#     time_encoding_size:       int  = 16
#     market_session_encoding:  bool = True
#     session_encoding_size:    int  = 8

#     # Trainer
#     lr:                   float = 2e-4
#     batch_size:           int   = 64
#     epochs:               int   = 30
#     patience:             int   = 4          # early stop
#     amp:                  bool  = True       # mixed precision

# # ---------- 2.  LoRA helper ---------------------------------------------------

# class LoRALinear(nn.Module):
#     """Base linear layer + strategy-specific LoRA adapters (A·B low-rank)"""
#     def __init__(self, in_f: int, out_f: int):
#         super().__init__()
#         self.base = nn.Linear(in_f, out_f)
#         self.adapters: dict[str, nn.ModuleDict] = {}

#     def add(self, name: str, rank: int, scale: float = 1.0):
#         if rank <= 0 or name in self.adapters: return
#         A = nn.Linear(self.base.in_features, rank, bias=False)
#         B = nn.Linear(rank, self.base.out_features, bias=False)
#         nn.init.kaiming_uniform_(A.weight, a=math.sqrt(5))
#         nn.init.zeros_(B.weight)
#         self.adapters[name] = nn.ModuleDict({"A": A, "B": B, "s": nn.Parameter(torch.tensor(scale))})

#     def forward(self, x: torch.Tensor, strategy: str | None = None):
#         y = self.base(x)
#         if strategy and strategy in self.adapters:
#             ad = self.adapters[strategy]
#             y = y + ad["s"] * ad["B"](ad["A"](x))
#         return y

# # ---------- 3.  Feature extraction -------------------------------------------

# class FeatureExtractor:
#     """Vectorises raw StockData dicts."""
#     CORE = [
#         "bid","ask","last","mid","spread","spreadPercent",
#         "bidSize","askSize","lastSize","volume",
#         "priceChange","momentum","imbalance","depthImbalance","vwap"
#     ]

#     TECH = ["rsi","ema9","ema26","alma","atr"]
#     VOL  = ["volume_ratio","volume_trend"]

#     def __init__(self, cfg: TradingLSTMConfig):
#         self.names = self.CORE + (
#             self.TECH if cfg.use_technical_indicators else []
#         ) + (
#             self.VOL if cfg.use_volume_features      else []
#         )
#         cfg.input_feature_size = len(self.names)

#     def __call__(self, seq: Sequence[dict]) -> torch.Tensor:
#         rows = []
#         for sd in seq:
#             r = [float(sd.get(key,0.0)) for key in self.names]
#             rows.append(r)
#         return torch.tensor(rows, dtype=torch.float32)

# # ---------- 4.  Time & session encoding --------------------------------------

# class TimeSessionEncoder(nn.Module):
#     def __init__(self,cfg:TradingLSTMConfig):
#         super().__init__()
#         self.cfg=cfg
#         if cfg.use_time_encoding:
#             self.time_proj = nn.Linear(4, cfg.time_encoding_size)
#         if cfg.market_session_encoding:
#             self.session_emb = nn.Embedding(5, cfg.session_encoding_size)

#     def forward(self, ts: torch.Tensor):           # [B,S] UNIX secs
#         encs=[]
#         if self.cfg.use_time_encoding:
#             h = (ts/3600.)%24
#             m = (ts/60.)%60
#             features = torch.stack([torch.sin(2*math.pi*h/24),
#                                     torch.cos(2*math.pi*h/24),
#                                     torch.sin(2*math.pi*m/60),
#                                     torch.cos(2*math.pi*m/60)],dim=-1)
#             encs.append(self.time_proj(features))
#         if self.cfg.market_session_encoding:
#             hrs = (ts/3600.).long()%24
#             sess = torch.full_like(hrs,4)
#             sess = torch.where(hrs<9,0,sess)
#             sess = torch.where((hrs>=9)&(hrs<12),1,sess)
#             sess = torch.where((hrs>=12)&(hrs<14),2,sess)
#             sess = torch.where((hrs>=14)&(hrs<16),3,sess)
#             encs.append(self.session_emb(sess))
#         return torch.cat(encs,dim=-1) if encs else None

# # ---------- 5.  Main model ----------------------------------------------------

# class TradingLSTM(nn.Module):
#     def __init__(self,cfg:TradingLSTMConfig):
#         super().__init__()
#         self.cfg=cfg
#         self.fe = FeatureExtractor(cfg)
#         self.ts_enc = TimeSessionEncoder(cfg)

#         in_sz = cfg.input_feature_size
#         if cfg.use_time_encoding:       in_sz += cfg.time_encoding_size
#         if cfg.market_session_encoding: in_sz += cfg.session_encoding_size

#         self.in_proj = LoRALinear(in_sz, cfg.hidden_size)
#         self.lstm = nn.LSTM(cfg.hidden_size, cfg.hidden_size,
#                             cfg.num_lstm_layers, batch_first=True,
#                             dropout=cfg.dropout if cfg.num_lstm_layers>1 else 0,
#                             bidirectional=cfg.bidirectional)
#         self.attn = nn.MultiheadAttention(cfg.hidden_size, 8, batch_first=True)
#         out_sz = cfg.hidden_size * (2 if cfg.bidirectional else 1)

#         self.head = nn.Sequential(
#             LoRALinear(out_sz, out_sz//2),
#             nn.ReLU(),
#             nn.Dropout(cfg.dropout),
#             LoRALinear(out_sz//2, cfg.num_classes if cfg.classification else 1)
#         )

#         # attach strategy adapters
#         for target in [self.in_proj, self.head[0], self.head[3]]:
#             target.add("momentum"    , cfg.lora_rank_momentum , cfg.lora_scale)
#             target.add("mean_revert" , cfg.lora_rank_mean_revert , cfg.lora_scale)
#             target.add("scalping"    , cfg.lora_rank_scalping , cfg.lora_scale)

#     # ------------- helpers ----------------------------------------------------
#     def _pack_inputs(self, seqs: Sequence[Sequence[dict]],
#                      ts: Optional[torch.Tensor])->torch.Tensor:
#         feats = [self.fe(s) for s in seqs]
#         x = torch.stack(feats)      # [B,S,F]
#         if ts is not None:
#             enc = self.ts_enc(ts.to(x.device))
#             if enc is not None: x = torch.cat([x,enc],dim=-1)
#         return x

#     # ------------- forward ----------------------------------------------------
#     def forward(self, seqs: Sequence[Sequence[dict]],
#                 timestamps: Optional[torch.Tensor]=None,
#                 strategy: Optional[str]=None) -> torch.Tensor:
#         x = self._pack_inputs(seqs,timestamps).to(next(self.parameters()).device)
#         x = self.in_proj(x,strategy)
#         lstm_out,_ = self.lstm(x)
#         attn_out,_ = self.attn(lstm_out,lstm_out,lstm_out)
#         feats = attn_out[:,-1]               # last step
#         y = self.head[0](feats,strategy)
#         y = self.head[1](y); y=self.head[2](y); y=self.head[3](y,strategy)
#         return y                             # logits or price

#     # ------------- convenience ------------------------------------------------
#     @torch.no_grad()
#     def predict(self, seq: Sequence[dict], device="mps",
#                 strategy:str|None=None)->Tuple[str,float]:
#         self.eval().to(device)
#         logits=self([seq], strategy=strategy).cpu()[0]
#         if self.cfg.classification:
#             probs=torch.softmax(logits,dim=-1)
#             idx=int(probs.argmax())
#             names=['StrongSell','Sell','Hold','Buy','StrongBuy']
#             return names[idx], float(probs[idx])
#         return "Price", float(logits.item())

# # ---------- 6.  Dataset / trainer --------------------------------------------

# class TickDataset(Dataset):
#     def __init__(self, seqs:List[List[dict]], targets:List[int]):
#         self.seqs, self.targets = seqs, targets
#     def __len__(self): return len(self.seqs)
#     def __getitem__(self,idx):
#         return self.seqs[idx], self.targets[idx]

# class Trainer:
#     def __init__(self, model:TradingLSTM, cfg:TradingLSTMConfig,
#                  train_ds:Dataset, val_ds:Dataset):
#         self.m,self.cfg=model, cfg
#         self.dev="mps" if torch.backends.mps.is_available() else "cpu"
#         self.m.to(self.dev)
#         self.opt=torch.optim.AdamW(self.m.parameters(), lr=cfg.lr)
#         self.scaler=GradScaler(enabled=cfg.amp)
#         self.tr=DataLoader(train_ds,batch_size=cfg.batch_size,shuffle=True)
#         self.va=DataLoader(val_ds  ,batch_size=cfg.batch_size)
#         self.crit=nn.CrossEntropyLoss()
#     def _step(self, batch, train:bool):
#         seqs, tgt = batch
#         tgt=torch.tensor(tgt, device=self.dev)
#         with (autocast(enabled=self.cfg.amp) if train else torch.no_grad()):
#             logits=self.m(seqs, strategy=None)  # could feed timestamp tensor
#             loss=self.crit(logits,tgt)
#         if train:
#             self.scaler.scale(loss).backward()
#             self.scaler.step(self.opt); self.scaler.update()
#             self.opt.zero_grad(set_to_none=True)
#         return float(loss.item())
#     def fit(self):
#         best_val, patience=1e9, self.cfg.patience
#         for epoch in range(1,self.cfg.epochs+1):
#             self.m.train(); tl=[self._step(b,True) for b in self.tr]
#             self.m.eval() ; vl=[self._step(b,False) for b in self.va]
#             tl,vl=sum(tl)/len(tl), sum(vl)/len(vl)
#             print(f"[{epoch:02}] train={tl:.4f}  val={vl:.4f}")
#             if vl<best_val: best_val,patience=vl,self.cfg.patience
#             else: patience-=1
#             if patience==0:
#                 print("Early stop."); break

# # ---------- 7.  C++ ⇄ Python bridge stubs ------------------------------------
# # Build with:  c++ -O3 -shared -std=c++20 `python3 -m pybind11 --includes` \
# #              bridge.cpp -o trading_bridge$(python3-config --extension-suffix)

# """
# // bridge.cpp  (minimal example)
# #include <pybind11/pybind11.h>
# #include <pybind11/stl.h>
# namespace py = pybind11;

# // Mirror of StockData from C++
# struct StockData {
#     double bid, ask, last, mid, spread, spreadPercent;
#     int    bidSize, askSize, lastSize, volume;
#     double priceChange, momentum, imbalance, depthImbalance, vwap;
#     // … plus RSI / EMA / etc.
# };

# PYBIND11_MODULE(trading_bridge, m) {
#     py::class_<StockData>(m,"StockData")
#        .def(py::init<>())
#        .def_readwrite("bid", &StockData::bid) /* … all fields … */;

#     // send a vector<StockData> into Python and get prediction back
#     m.def("predict_signal", [](const std::vector<StockData>& seq,
#                                const std::string& strat){
#         py::gil_scoped_acquire gil;
#         auto trading_lstm = py::module_::import("trading_lstm");
#         auto result = trading_lstm.attr("py_predict")(seq,strat);
#         return result;
#     });
# }
# """

# # Helper exposed to C++
# def py_predict(seq: Sequence[Any], strategy: str|None):
#     """Wrapped by C++ bridge – converts C++ StockData -> dict list."""
#     dict_seq=[sd.__dict__ if hasattr(sd,"__dict__") else sd for sd in seq]
#     global _global_model
#     if _global_model is None:
#         _global_model = TradingLSTM(TradingLSTMConfig())  # load weights…
#     return _global_model.predict(dict_seq,strategy=strategy)

# _global_model: TradingLSTM | None = None

# # ---------- 8.  Example usage -------------------------------------------------

# if __name__ == "__main__":
#     cfg = TradingLSTMConfig()
#     model = TradingLSTM(cfg)
#     # dummy data
#     seq = [{"bid":10,"ask":10.1,"last":10.05,"mid":10.05,"spread":0.05,
#             "spreadPercent":0.5,"bidSize":100,"askSize":120,"lastSize":80,
#             "volume":1000,"priceChange":0,"momentum":0,"imbalance":0,
#             "depthImbalance":0,"vwap":10.04,"rsi":50}] * cfg.sequence_length
#     print(model.predict(seq))
