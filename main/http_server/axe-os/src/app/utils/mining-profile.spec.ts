import { AsicProfileInfo, computeMiningProfile, efficiencyJTH } from './mining-profile';

describe('mining-profile', () => {
  // Gamma-class mock device per spec defaults.
  const gamma: AsicProfileInfo = {
    defaultFrequency: 525,
    frequencyOptions: [400, 425, 450, 475, 500, 525, 550, 575, 600, 625],
    defaultVoltage: 1150,
    voltageOptions: [1100, 1150, 1200, 1250]
  };

  describe('computeMiningProfile', () => {
    it('balanced returns defaults and asgErrorTarget 2', () => {
      expect(computeMiningProfile('balanced', gamma)).toEqual({
        frequency: 525,
        coreVoltage: 1150,
        asgErrorTarget: 2
      });
    });

    it('eco steps one option below default and asgErrorTarget 1', () => {
      expect(computeMiningProfile('eco', gamma)).toEqual({
        frequency: 500,
        coreVoltage: 1100,
        asgErrorTarget: 1
      });
    });

    it('turbo steps one option above default and asgErrorTarget 3', () => {
      expect(computeMiningProfile('turbo', gamma)).toEqual({
        frequency: 550,
        coreVoltage: 1200,
        asgErrorTarget: 3
      });
    });

    it('clamps eco at the lowest option', () => {
      const asic: AsicProfileInfo = {
        defaultFrequency: 400,
        frequencyOptions: [400, 425, 450],
        defaultVoltage: 1100,
        voltageOptions: [1100, 1150, 1200]
      };
      expect(computeMiningProfile('eco', asic)).toEqual({
        frequency: 400,
        coreVoltage: 1100,
        asgErrorTarget: 1
      });
    });

    it('clamps turbo at the highest option', () => {
      const asic: AsicProfileInfo = {
        defaultFrequency: 450,
        frequencyOptions: [400, 425, 450],
        defaultVoltage: 1200,
        voltageOptions: [1100, 1150, 1200]
      };
      expect(computeMiningProfile('turbo', asic)).toEqual({
        frequency: 450,
        coreVoltage: 1200,
        asgErrorTarget: 3
      });
    });

    it('sorts unsorted options before locating the default', () => {
      const asic: AsicProfileInfo = {
        defaultFrequency: 500,
        frequencyOptions: [550, 400, 500, 450, 600],
        defaultVoltage: 1200,
        voltageOptions: [1250, 1100, 1200, 1150]
      };
      expect(computeMiningProfile('eco', asic)).toEqual({
        frequency: 450,
        coreVoltage: 1150,
        asgErrorTarget: 1
      });
      expect(computeMiningProfile('turbo', asic)).toEqual({
        frequency: 550,
        coreVoltage: 1250,
        asgErrorTarget: 3
      });
    });

    it('uses nearest option when default is not in the list', () => {
      const asic: AsicProfileInfo = {
        defaultFrequency: 510,
        frequencyOptions: [400, 450, 500, 550],
        defaultVoltage: 1150,
        voltageOptions: [1100, 1150, 1200]
      };
      // nearest to 510 is 500 (index 2) -> turbo steps to 550
      expect(computeMiningProfile('turbo', asic).frequency).toBe(550);
    });

    it('falls back to defaults when options arrays are empty', () => {
      const asic: AsicProfileInfo = {
        defaultFrequency: 525,
        frequencyOptions: [],
        defaultVoltage: 1150,
        voltageOptions: []
      };
      expect(computeMiningProfile('eco', asic)).toEqual({
        frequency: 525,
        coreVoltage: 1150,
        asgErrorTarget: 1
      });
      expect(computeMiningProfile('balanced', asic)).toEqual({
        frequency: 525,
        coreVoltage: 1150,
        asgErrorTarget: 2
      });
      expect(computeMiningProfile('turbo', asic)).toEqual({
        frequency: 525,
        coreVoltage: 1150,
        asgErrorTarget: 3
      });
    });

    it('never returns NaN/undefined when one options array is missing', () => {
      const asic = {
        defaultFrequency: 525,
        frequencyOptions: [500, 525, 550],
        defaultVoltage: 1150,
        voltageOptions: undefined as unknown as number[]
      } as AsicProfileInfo;
      const turbo = computeMiningProfile('turbo', asic);
      expect(turbo.frequency).toBe(550);
      expect(turbo.coreVoltage).toBe(1150);
      expect(Number.isNaN(turbo.coreVoltage)).toBeFalse();
    });
  });

  describe('efficiencyJTH', () => {
    it('computes W per TH/s', () => {
      // 11.67 W at 475 GH/s -> 11.67 / 0.475 TH/s
      expect(efficiencyJTH(11.67, 475)).toBeCloseTo(24.568, 3);
    });

    it('returns a round number for clean inputs', () => {
      // 20 W at 1000 GH/s (1 TH/s) -> 20 J/TH
      expect(efficiencyJTH(20, 1000)).toBe(20);
    });

    it('returns null when hashrate is zero', () => {
      expect(efficiencyJTH(15, 0)).toBeNull();
    });

    it('returns null when hashrate is negative', () => {
      expect(efficiencyJTH(15, -5)).toBeNull();
    });

    it('returns null when power is zero or negative', () => {
      expect(efficiencyJTH(0, 475)).toBeNull();
      expect(efficiencyJTH(-1, 475)).toBeNull();
    });
  });
});
