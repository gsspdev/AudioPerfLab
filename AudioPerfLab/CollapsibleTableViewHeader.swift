/*
 * Copyright (c) 2019 Ableton AG, Berlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

import UIKit

class CollapsibleTableViewHeader: UIView {
  var onTap = {}

  var title: String? {
    get { return titleLabel.text }
    set { titleLabel.attributedText = makeAttributedString(text: newValue ?? "") }
  }

  var value: String? {
    get { return valueLabel.text }
    set { valueLabel.attributedText = makeAttributedString(text: newValue ?? "") }
  }

  var isExpanded : Bool {
    get { return disclosureIndicator.isExpanded }
    set { disclosureIndicator.isExpanded = newValue }
  }

  private let titleLabel = UILabel()
  private let valueLabel = UILabel()
  private let disclosureIndicator = DisclosureIndicatorView()

  override init(frame: CGRect) {
    super.init(frame: frame)

    addSubview(titleLabel)
    titleLabel.translatesAutoresizingMaskIntoConstraints = false

    addSubview(valueLabel)
    valueLabel.translatesAutoresizingMaskIntoConstraints = false

    addSubview(disclosureIndicator)
    disclosureIndicator.translatesAutoresizingMaskIntoConstraints = false

    let views = [
      "title": titleLabel,
      "value": valueLabel,
      "disclosureIndicator": disclosureIndicator]

    var allConstraints: [NSLayoutConstraint] = []
    allConstraints += NSLayoutConstraint.constraints(
      withVisualFormat: "V:|-[title]-|", metrics: nil, views: views)
    allConstraints += NSLayoutConstraint.constraints(
      withVisualFormat: "V:|-[value]-|", metrics: nil, views: views)
    allConstraints += NSLayoutConstraint.constraints(
      withVisualFormat: "V:|-[disclosureIndicator]-|", metrics: nil, views: views)
    allConstraints += NSLayoutConstraint.constraints(
      withVisualFormat: "H:|-16-[title]-[value]-16-[disclosureIndicator]-20-|",
      metrics: nil,
      views: views)
    NSLayoutConstraint.activate(allConstraints)

    addGestureRecognizer(UITapGestureRecognizer(
      target:self, action: #selector(CollapsibleTableViewHeader.tapHeader(_:))))
  }

  required init?(coder aDecoder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  private func makeAttributedString(text: String) -> NSAttributedString {
    return NSAttributedString(string: text, attributes: [
      .font: CollapsibleTableViewHeader.font(),
      .foregroundColor: UIColor.gray,
      .baselineOffset: 1.0
    ])
  }

  private static func font() -> UIFont {
    let settings: [[UIFontDescriptor.FeatureKey: Int]] = []
    let descriptor = UIFont.systemFont(ofSize: 19.0, weight: .light)
      .fontDescriptor.addingAttributes([.featureSettings: settings])
    return UIFont(descriptor: descriptor, size: descriptor.pointSize)
  }

  @objc private func tapHeader(_ gestureRecognizer: UITapGestureRecognizer) {
    isExpanded = !isExpanded
    onTap()
  }
}
