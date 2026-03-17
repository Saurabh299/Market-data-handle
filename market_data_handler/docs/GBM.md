# GBM.md – Geometric Brownian Motion for Market Data Simulation

## 1. Mathematical Background

### 1a. Stochastic Differential Equation

Stock prices are modelled as a continuous-time stochastic process `S(t)` satisfying:

```
dS = μ · S · dt  +  σ · S · dW
```

Where:
- `S`  — current price
- `μ`  — drift coefficient (annualised expected return)
- `σ`  — volatility coefficient (annualised standard deviation of returns)
- `dt` — infinitesimal time step
- `dW` — Wiener process increment: `dW ~ N(0, dt)`, i.e. `dW = Z·√dt`, `Z ~ N(0,1)`

This is the **Black-Scholes model** for an asset with log-normally distributed returns.

### 1b. Discretisation for Simulation

Applying Itô's lemma to `ln(S)`, the exact discretisation (no Euler error) is:

```
S(t + dt) = S(t) · exp( (μ - σ²/2)·dt  +  σ·√dt·Z )
```

where `Z ~ N(0, 1)` is a standard normal random variable.

The term `(μ - σ²/2)` is the **Itô correction**: without it, the expected value of `S` would be biased downward. With it, `E[S(t+dt)] = S(t)·exp(μ·dt)` exactly.

### 1c. Why GBM for Stock Prices?

GBM is the standard starting point for equity price simulation because:

1. **Non-negativity**: `S` can never go below zero (exponential process).
2. **Log-normal returns**: empirically, short-horizon equity returns are approximately log-normal.
3. **Multiplicative shocks**: a 1% move has the same economic significance whether the price is ₹100 or ₹5000.
4. **Tractability**: closed-form solutions exist for options pricing (Black-Scholes).

Limitations (not modelled here): fat tails, volatility clustering (GARCH), mean reversion, jumps. These are left as extensions.

---

## 2. Implementation Details

### 2a. Box-Muller Transform for N(0,1) Samples

Standard uniform random numbers `U1, U2 ~ Uniform(0,1)` are transformed into two independent standard normals:

```
Z1 = sqrt(-2 · ln(U1)) · cos(2π · U2)
Z2 = sqrt(-2 · ln(U1)) · sin(2π · U2)
```

Implementation in `TickGenerator::normal_sample()`:

```cpp
// Polar form (avoids trig, faster):
do {
    u = dist_(rng_) * 2.0 - 1.0;   // U[-1,1]
    v = dist_(rng_) * 2.0 - 1.0;
    s = u*u + v*v;
} while (s >= 1.0 || s == 0.0);    // rejection: inside unit circle

double mul = sqrt(-2.0 * log(s) / s);
Z1 = u * mul;   // returned
Z2 = v * mul;   // cached for next call (has_spare_ flag)
```

The polar form avoids `cos`/`sin` and is approximately 20% faster than the trigonometric variant.

The `mt19937_64` Mersenne Twister PRNG is used for high-quality pseudo-random numbers with a period of 2^19937 − 1, sufficient for billions of simulation steps.

### 2b. Parameter Selection Rationale

| Symbol class | μ (drift) | σ (volatility) | dt |
|---|---|---|---|
| Neutral (70%) | 0.00 | 0.01–0.06 | 0.001 |
| Bull trend (15%) | +0.05 | 0.01–0.06 | 0.001 |
| Bear trend (15%) | −0.05 | 0.01–0.06 | 0.001 |

- **μ = ±0.05**: Represents ±5% annualised drift, typical for moderately trending stocks.
- **σ ∈ [0.01, 0.06]**: 1% (defensive blue-chip) to 6% (volatile small-cap) daily volatility. NSE large-caps typically exhibit 1.5–2.5% daily σ.
- **dt = 0.001**: One millisecond time step. At 100K ticks/s across 100 symbols, each symbol updates ~1000 times/second, so 1ms per step is accurate.

### 2c. Time Step Considerations

At `dt = 0.001` (1ms) and `σ = 0.02` (2% daily vol), the per-tick price standard deviation is:

```
σ·√dt = 0.02 · √0.001 ≈ 0.00063  (0.063% per tick)
```

For a ₹1000 stock, this is approximately ₹0.63 per tick — realistic for liquid NSE instruments.

If `dt` is too large (e.g., 1.0 = 1 second), price moves become unrealistically large in a single tick. If too small (e.g., 1e-6), many ticks produce negligible movement, reducing the simulation's market realism.

---

## 3. Realism Considerations

### 3a. Bid-Ask Spread

The spread is modelled as a random fraction of the mid-price:

```
spread_fraction ~ Uniform(0.05%, 0.20%)
half_spread = price · spread_fraction / 2
bid = price − half_spread
ask = price + half_spread
```

This matches the NSE tick structure where liquid large-caps (RELIANCE, TCS) have sub-0.1% spreads and less liquid mid-caps have spreads up to 0.2%.

### 3b. Volume Generation

Trade and quote quantities follow a **log-normal distribution**:

```
Quote volume: LogNormal(μ=6.9, σ=0.5)   → mean ≈ 1000 shares
Trade volume: LogNormal(μ=5.5, σ=0.8)   → mean ≈ 245 shares
```

Log-normal is appropriate because volume is strictly positive and right-skewed (occasional block trades are much larger than typical retail orders).

### 3c. Message Mix (Quote vs Trade)

The 70%/30% quote-to-trade ratio approximates NSE's order book dynamics:
- Most ticks are quote updates as market makers reprice (bid/ask shift with each GBM step)
- Trades occur less frequently and consume liquidity from the book

### 3d. Sequence Numbers

Each message carries a strictly increasing `uint32_t` sequence number. The simulator intentionally injects sequence gaps (when `--fault` is passed) at a rate of ~1% to test the parser's gap detection logic.

---

## 4. Possible Extensions

| Extension | Description |
|-----------|-------------|
| Heston model | Stochastic volatility (σ itself follows a mean-reverting process) |
| Jump-diffusion | Add Poisson jumps for earnings announcements / circuit breakers |
| Correlation | Correlated price moves across symbols (factor model) |
| Mean reversion | Ornstein-Uhlenbeck process for interest-rate instruments |
| Microstructure noise | Add bid-ask bounce and rounding to nearest tick size |
